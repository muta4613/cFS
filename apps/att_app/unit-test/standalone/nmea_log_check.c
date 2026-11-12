/************************************************************************
 * ATT_APP - GNSS signal-health monitor for NASA cFS
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ************************************************************************/

/*
 * Standalone check of the ATT_APP NMEA parser.
 *
 * Builds with nothing but a C compiler (no cFE, no OSAL library), which is
 * the point: the parser has no flight-software dependencies. Run it under
 * AddressSanitizer / UndefinedBehaviorSanitizer (see the Makefile) to turn
 * any out-of-bounds access into a hard failure.
 *
 *   1. Replays a recorded log through the epoch assembler and checks the
 *      totals against what the caller expects.
 *   2. Corrupts every sentence of the log in several ways and feeds the
 *      results to every parser entry point. Each corrupted sentence is
 *      copied into an exactly-sized heap block first, so reading even one
 *      byte past the end is caught by the sanitizer.
 *
 * Usage: nmea_log_check <log file> <expected epochs> <expected no-fix epochs>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "att_app_nmea.h"

#define CHECK_LINE_BUF_SIZE  256
#define CHECK_MUTATIONS      64
#define CHECK_LCG_MULTIPLIER 1664525u
#define CHECK_LCG_INCREMENT  1013904223u
#define CHECK_LCG_SEED       4613u
#define CHECK_LCG_SHIFT      8
#define CHECK_BYTE_VALUES    256
#define CHECK_MUTATION_KINDS 5
#define CHECK_MAX_GROWTH     200
#define CHECK_REQUIRED_ARGC  4
#define CHECK_ARG_LOG        1
#define CHECK_ARG_EPOCHS     2
#define CHECK_ARG_NOFIX      3
#define CHECK_DECIMAL        10

static unsigned int CheckRandState = CHECK_LCG_SEED;

/* Small deterministic generator so every run corrupts the log the same way */
static unsigned int CheckRand(unsigned int Limit)
{
    CheckRandState = CheckRandState * CHECK_LCG_MULTIPLIER + CHECK_LCG_INCREMENT;
    return (CheckRandState >> CHECK_LCG_SHIFT) % Limit;
}

/* Run every public entry point over one buffer; results are deliberately ignored */
static void CheckExercise(const char *Data, size_t Length, ATT_APP_NMEA_Assembler_t *Assembler)
{
    ATT_APP_NMEA_Gga_t   Gga;
    ATT_APP_NMEA_Gsv_t   Gsv;
    ATT_APP_NMEA_Epoch_t Epoch;
    bool                 Ready;

    (void)ATT_APP_NMEA_VerifyChecksum(Data, Length);
    (void)ATT_APP_NMEA_GetSentenceType(Data, Length);
    (void)ATT_APP_NMEA_ParseGga(Data, Length, &Gga);
    (void)ATT_APP_NMEA_ParseGsv(Data, Length, &Gsv);
    (void)ATT_APP_NMEA_AssemblerFeed(Assembler, Data, Length, &Epoch, &Ready);
}

/* Corrupt one sentence in a randomly chosen way and exercise the parser with it */
static void CheckMutate(const char *Line, size_t Length, ATT_APP_NMEA_Assembler_t *Assembler)
{
    size_t NewLength = Length;
    char  *Copy;
    size_t i;

    switch (CheckRand(CHECK_MUTATION_KINDS))
    {
        case 0: /* truncate */
            NewLength = CheckRand((unsigned int)Length + 1);
            break;
        case 1: /* grow far beyond the legal sentence length */
            NewLength = Length + CheckRand(CHECK_MAX_GROWTH);
            break;
        default: /* same length, bytes changed below */
            break;
    }

    /* malloc(0) may return NULL; one spare byte keeps the pointer valid but is never passed as length */
    Copy = malloc(NewLength + 1);
    if (Copy == NULL)
    {
        fprintf(stderr, "out of memory\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < NewLength; ++i)
    {
        Copy[i] = (i < Length) ? Line[i] : ',';
    }

    if (NewLength > 0)
    {
        switch (CheckRand(CHECK_MUTATION_KINDS))
        {
            case 0: /* flip one byte to anything, including NUL and non-ASCII */
                Copy[CheckRand((unsigned int)NewLength)] = (char)CheckRand(CHECK_BYTE_VALUES);
                break;
            case 1: /* empty a field by turning a character into a separator */
                Copy[CheckRand((unsigned int)NewLength)] = ',';
                break;
            case 2: /* turn a separator-ish position into a digit to make numbers too long */
                Copy[CheckRand((unsigned int)NewLength)] = '9';
                break;
            case 3: /* second start character in the middle */
                Copy[CheckRand((unsigned int)NewLength)] = '$';
                break;
            default: /* leave as is: pure truncation / growth */
                break;
        }
    }

    /* Realloc to the exact size so the sanitizer's red zone starts right at NewLength */
    if (NewLength > 0)
    {
        char *Exact = malloc(NewLength);
        if (Exact == NULL)
        {
            fprintf(stderr, "out of memory\n");
            exit(EXIT_FAILURE);
        }
        memcpy(Exact, Copy, NewLength);
        CheckExercise(Exact, NewLength, Assembler);
        free(Exact);
    }
    else
    {
        CheckExercise(Copy, 0, Assembler);
    }

    free(Copy);
}

int main(int argc, char *argv[])
{
    ATT_APP_NMEA_Assembler_t Assembler;
    ATT_APP_NMEA_Assembler_t FuzzAssembler;
    ATT_APP_NMEA_Epoch_t     Epoch;
    char                     Line[CHECK_LINE_BUF_SIZE];
    FILE                    *File;
    bool                     Ready;
    long                     ExpectedEpochs;
    long                     ExpectedNoFix;
    long                     Epochs    = 0;
    long                     NoFix     = 0;
    long                     Sentences = 0;
    long                     Errors    = 0;
    long                     Mutations = 0;
    long                     ShortSats = 0;
    int                      i;

    if (argc != CHECK_REQUIRED_ARGC)
    {
        fprintf(stderr, "usage: %s <log file> <expected epochs> <expected no-fix epochs>\n", argv[0]);
        return EXIT_FAILURE;
    }

    ExpectedEpochs = strtol(argv[CHECK_ARG_EPOCHS], NULL, CHECK_DECIMAL);
    ExpectedNoFix  = strtol(argv[CHECK_ARG_NOFIX], NULL, CHECK_DECIMAL);

    File = fopen(argv[CHECK_ARG_LOG], "rb");
    if (File == NULL)
    {
        perror(argv[CHECK_ARG_LOG]);
        return EXIT_FAILURE;
    }

    ATT_APP_NMEA_AssemblerInit(&Assembler);
    ATT_APP_NMEA_AssemblerInit(&FuzzAssembler);

    while (fgets(Line, sizeof(Line), File) != NULL)
    {
        size_t Length = strlen(Line);

        ++Sentences;

        if (ATT_APP_NMEA_AssemblerFeed(&Assembler, Line, Length, &Epoch, &Ready) != ATT_APP_NMEA_OK)
        {
            ++Errors;
            fprintf(stderr, "rejected sentence %ld: %s", Sentences, Line);
        }
        else if (Ready)
        {
            ++Epochs;
            NoFix     += (Epoch.Gga.FixQuality == ATT_APP_NMEA_FIX_QUALITY_NONE) ? 1 : 0;
            ShortSats += (Epoch.NumSats != Epoch.SatsInView) ? 1 : 0;
        }

        for (i = 0; i < CHECK_MUTATIONS; ++i)
        {
            CheckMutate(Line, Length, &FuzzAssembler);
            ++Mutations;
        }
    }

    fclose(File);

    if (ATT_APP_NMEA_AssemblerFlush(&Assembler, &Epoch))
    {
        ++Epochs;
        NoFix     += (Epoch.Gga.FixQuality == ATT_APP_NMEA_FIX_QUALITY_NONE) ? 1 : 0;
        ShortSats += (Epoch.NumSats != Epoch.SatsInView) ? 1 : 0;
    }

    printf("%s: %ld sentences, %ld epochs (%ld without fix), %ld rejected, %ld incomplete epochs, "
           "%ld corrupted inputs survived\n",
           argv[CHECK_ARG_LOG],
           Sentences,
           Epochs,
           NoFix,
           Errors,
           ShortSats,
           Mutations);

    if (Errors != 0 || ShortSats != 0 || Epochs != ExpectedEpochs || NoFix != ExpectedNoFix)
    {
        fprintf(stderr,
                "FAIL: expected %ld epochs, %ld without fix, and no rejected sentences\n",
                ExpectedEpochs,
                ExpectedNoFix);
        return EXIT_FAILURE;
    }

    printf("PASS\n");
    return EXIT_SUCCESS;
}
