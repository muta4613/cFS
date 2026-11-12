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
** File: coveragetest_att_app_nmea.c
**
** Purpose:
** Coverage Unit Test cases for the ATT_APP NMEA parser and epoch assembler.
**
** Notes:
** The parser is a set of pure functions, so these tests need no stubs:
** they pass in text and check what comes out.
*/

/*
 * Includes
 */
#include "att_app_coveragetest_common.h"
#include "att_app_nmea.h"

#include <math.h>

/* Largest difference tolerated when comparing floating point results */
#define UT_NMEA_EPSILON 1e-6

/* Sentences taken from the committed sample log (checksums are real) */
#define UT_GGA_FIX   "$GPGGA,180000.00,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,*64"
#define UT_GGA_NOFIX "$GPGGA,180352.00,,,,,0,03,,,M,,M,,*46"
#define UT_GSV_1OF3  "$GPGSV,3,1,10,02,62,045,47,05,48,132,44,07,71,301,47,09,23,210,41*79"
#define UT_GSV_2OF3  "$GPGSV,3,2,10,13,35,088,41,15,15,322,39,18,55,175,45,21,41,255,44*7E"
#define UT_GSV_3OF3  "$GPGSV,3,3,10,26,19,060,40,30,30,290,41*78"
#define UT_GSV_EMPTY "$GPGSV,3,1,10,02,60,047,45,05,50,134,,07,73,303,47,09,25,212,*75"

/*
 * Build "$<Body>*hh" with a correct checksum, so a test can describe a
 * sentence by its content alone. Returns the sentence length.
 */
static size_t UT_NMEA_Make(char *Buf, size_t BufSize, const char *Body)
{
    uint8  Sum = 0;
    size_t i;

    for (i = 0; Body[i] != '\0'; ++i)
    {
        Sum ^= (uint8)Body[i];
    }

    snprintf(Buf, BufSize, "$%s*%02X", Body, (unsigned int)Sum);

    return strlen(Buf);
}

/* Parse a GGA described by its body and return the status */
static ATT_APP_NMEA_Status_t UT_NMEA_Gga(const char *Body, ATT_APP_NMEA_Gga_t *Gga)
{
    char   Buf[2 * ATT_APP_NMEA_MAX_SENTENCE_LEN];
    size_t Len = UT_NMEA_Make(Buf, sizeof(Buf), Body);

    return ATT_APP_NMEA_ParseGga(Buf, Len, Gga);
}

/* Parse a GSV described by its body and return the status */
static ATT_APP_NMEA_Status_t UT_NMEA_Gsv(const char *Body, ATT_APP_NMEA_Gsv_t *Gsv)
{
    char   Buf[2 * ATT_APP_NMEA_MAX_SENTENCE_LEN];
    size_t Len = UT_NMEA_Make(Buf, sizeof(Buf), Body);

    return ATT_APP_NMEA_ParseGsv(Buf, Len, Gsv);
}

/* Feed a literal sentence to an assembler */
static ATT_APP_NMEA_Status_t
UT_NMEA_Feed(ATT_APP_NMEA_Assembler_t *Assembler, const char *Sentence, ATT_APP_NMEA_Epoch_t *Epoch, bool *Ready)
{
    return ATT_APP_NMEA_AssemblerFeed(Assembler, Sentence, strlen(Sentence), Epoch, Ready);
}

/*
**********************************************************************************
**          TEST CASE FUNCTIONS
**********************************************************************************
*/

void Test_ATT_APP_NMEA_VerifyChecksum(void)
{
    /*
     * Test Case For:
     * ATT_APP_NMEA_Status_t ATT_APP_NMEA_VerifyChecksum(const char *Sentence, size_t Length)
     */
    char   Buf[4 * ATT_APP_NMEA_MAX_SENTENCE_LEN];
    char   Body[2 * ATT_APP_NMEA_MAX_SENTENCE_LEN];
    size_t Len;

    /* Valid sentences, with and without line terminators */
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum(UT_GGA_FIX, strlen(UT_GGA_FIX)), ATT_APP_NMEA_OK);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum(UT_GSV_3OF3 "\r\n", strlen(UT_GSV_3OF3) + 2), ATT_APP_NMEA_OK);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum(UT_GSV_3OF3 "\n", strlen(UT_GSV_3OF3) + 1), ATT_APP_NMEA_OK);

    /* Lower case hex digits in the checksum are tolerated */
    UtAssert_INT32_EQ(
        ATT_APP_NMEA_VerifyChecksum("$GPGSV,3,2,10,13,35,088,41,15,15,322,39,18,55,175,45,21,41,255,44*7e",
                                    strlen(UT_GSV_2OF3)),
        ATT_APP_NMEA_OK);

    /* NULL pointer */
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum(NULL, 10), ATT_APP_NMEA_ERR_NULL);

    /* Empty, only terminators, too short */
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("", 0), ATT_APP_NMEA_ERR_LENGTH);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("\r\n", 2), ATT_APP_NMEA_ERR_LENGTH);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GP*17", 6), ATT_APP_NMEA_ERR_LENGTH);

    /* Longest legal sentence passes, one character more does not */
    memset(Body, 'A', sizeof(Body));
    Body[ATT_APP_NMEA_MAX_SENTENCE_LEN - 4] = '\0';
    Len                                     = UT_NMEA_Make(Buf, sizeof(Buf), Body);
    UtAssert_UINT32_EQ(Len, ATT_APP_NMEA_MAX_SENTENCE_LEN);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum(Buf, Len), ATT_APP_NMEA_OK);

    memset(Body, 'A', sizeof(Body));
    Body[ATT_APP_NMEA_MAX_SENTENCE_LEN - 3] = '\0';
    Len                                     = UT_NMEA_Make(Buf, sizeof(Buf), Body);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum(Buf, Len), ATT_APP_NMEA_ERR_LENGTH);

    /* Missing '$', missing '*' */
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("GPGGA,180000.00*64", 18), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGGA,180000.0064", 18), ATT_APP_NMEA_ERR_FRAMING);

    /* Checksum digits that are not hex (each position) */
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,1,00*G9", 16), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,1,00*7Z", 16), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,1,00*7/", 16), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,1,00*7g", 16), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,1,00*7@", 16), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,1,00*7`", 16), ATT_APP_NMEA_ERR_FRAMING);

    /* Characters that may not appear in a body: control, non-ASCII, '$' and '*' */
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,\x01,00*79", 16), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,\xC3,00*79", 16), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,$,00*79", 16), ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,*,00*79", 16), ATT_APP_NMEA_ERR_FRAMING);

    /* Well framed but the checksum is wrong, or one data character was corrupted */
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,1,1,00*78", 16), ATT_APP_NMEA_ERR_CHECKSUM);
    UtAssert_INT32_EQ(ATT_APP_NMEA_VerifyChecksum("$GPGSV,3,3,10,26,19,060,40,30,30,290,42*78", strlen(UT_GSV_3OF3)),
                      ATT_APP_NMEA_ERR_CHECKSUM);
}

void Test_ATT_APP_NMEA_GetSentenceType(void)
{
    /*
     * Test Case For:
     * ATT_APP_NMEA_SentenceType_t ATT_APP_NMEA_GetSentenceType(const char *Sentence, size_t Length)
     */
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType(UT_GGA_FIX, strlen(UT_GGA_FIX)), ATT_APP_NMEA_SentenceType_GGA);
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType(UT_GSV_1OF3, strlen(UT_GSV_1OF3)), ATT_APP_NMEA_SentenceType_GSV);

    /* Any talker ID is accepted */
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType("$GNGGA,1*00", 11), ATT_APP_NMEA_SentenceType_GGA);
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType("$GLGSV,1*00", 11), ATT_APP_NMEA_SentenceType_GSV);

    /* Other sentence types, and an address ended by '*' instead of ',' */
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType("$GPRMC,1*00", 11), ATT_APP_NMEA_SentenceType_OTHER);
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType("$GPTXT*00", 9), ATT_APP_NMEA_SentenceType_OTHER);

    /* Not a sentence */
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType(NULL, 20), ATT_APP_NMEA_SentenceType_INVALID);
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType("$GPGGA", 6), ATT_APP_NMEA_SentenceType_INVALID);
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType("GPGGA,1,2*00", 12), ATT_APP_NMEA_SentenceType_INVALID);
    UtAssert_INT32_EQ(ATT_APP_NMEA_GetSentenceType("$GPGGAX,1*00", 12), ATT_APP_NMEA_SentenceType_INVALID);
}

void Test_ATT_APP_NMEA_ParseGga_Valid(void)
{
    /*
     * Test Case For:
     * ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGga(const char *Sentence, size_t Length, ATT_APP_NMEA_Gga_t *Gga)
     *
     * Sentences a receiver can legitimately send.
     */
    ATT_APP_NMEA_Gga_t Gga;

    /* Normal fix, taken from the sample log */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGga(UT_GGA_FIX, strlen(UT_GGA_FIX), &Gga), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gga.UtcHour, 18);
    UtAssert_UINT32_EQ(Gga.UtcMinute, 0);
    UtAssert_UINT32_EQ(Gga.UtcSecond, 0);
    UtAssert_UINT32_EQ(Gga.UtcCentisecond, 0);
    UtAssert_UINT32_EQ(Gga.FixQuality, 1);
    UtAssert_UINT32_EQ(Gga.SatsUsed, 10);
    UtAssert_True(fabs(Gga.Latitude - (39.0 + 44.3982 / 60.0)) < UT_NMEA_EPSILON, "Latitude %.7f", Gga.Latitude);
    UtAssert_True(fabs(Gga.Longitude + (104.0 + 59.4008 / 60.0)) < UT_NMEA_EPSILON, "Longitude %.7f", Gga.Longitude);
    UtAssert_True(fabs(Gga.Altitude - 1599.4) < 1e-3, "Altitude %.2f", (double)Gga.Altitude);

    /* Southern and eastern hemispheres, negative altitude, fractional seconds */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GNGGA,235959.5,3351.0000,S,15112.5000,E,2,07,1.1,-12.5,M,20.0,M,,", &Gga),
                      ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gga.UtcHour, 23);
    UtAssert_UINT32_EQ(Gga.UtcMinute, 59);
    UtAssert_UINT32_EQ(Gga.UtcSecond, 59);
    UtAssert_UINT32_EQ(Gga.UtcCentisecond, 50);
    UtAssert_UINT32_EQ(Gga.FixQuality, 2);
    UtAssert_True(fabs(Gga.Latitude + 33.85) < UT_NMEA_EPSILON, "Latitude %.7f", Gga.Latitude);
    UtAssert_True(fabs(Gga.Longitude - 151.208333333) < UT_NMEA_EPSILON, "Longitude %.7f", Gga.Longitude);
    UtAssert_True(fabs(Gga.Altitude + 12.5) < 1e-3, "Altitude %.2f", (double)Gga.Altitude);

    /* Time without a fraction, and with more digits than are kept */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,010203,0000.0000,N,00000.0000,E,1,04,,0,M,,M,,", &Gga), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gga.UtcSecond, 3);
    UtAssert_UINT32_EQ(Gga.UtcCentisecond, 0);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,010203.987,0000.0000,N,00000.0000,E,1,04,,0,M,,M,,", &Gga), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gga.UtcCentisecond, 98);

    /* Limits: poles, antimeridian, leap second */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,235960.00,9000.0000,N,18000.0000,W,1,04,,0,M,,M,,", &Gga), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gga.UtcSecond, 60);
    UtAssert_True(fabs(Gga.Latitude - 90.0) < UT_NMEA_EPSILON, "Latitude %.7f", Gga.Latitude);
    UtAssert_True(fabs(Gga.Longitude + 180.0) < UT_NMEA_EPSILON, "Longitude %.7f", Gga.Longitude);

    /* No fix: position fields are empty and that is not an error */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGga(UT_GGA_NOFIX, strlen(UT_GGA_NOFIX), &Gga), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gga.FixQuality, ATT_APP_NMEA_FIX_QUALITY_NONE);
    UtAssert_UINT32_EQ(Gga.SatsUsed, 3);
    UtAssert_UINT32_EQ(Gga.UtcMinute, 3);
    UtAssert_UINT32_EQ(Gga.UtcSecond, 52);
    UtAssert_True(Gga.Latitude == 0.0 && Gga.Longitude == 0.0 && Gga.Altitude == 0.0f, "Position zeroed without a fix");

    /* No fix: stale position text is ignored, not parsed */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,010203.00,garbage,N,garbage,W,0,00,,,M,,M,,", &Gga), ATT_APP_NMEA_OK);
    UtAssert_True(Gga.Latitude == 0.0, "Position ignored without a fix");
}

void Test_ATT_APP_NMEA_ParseGga_Malformed(void)
{
    /*
     * Test Case For:
     * ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGga(const char *Sentence, size_t Length, ATT_APP_NMEA_Gga_t *Gga)
     *
     * Every way a GGA can be wrong. In each case the output must stay zeroed.
     */
    ATT_APP_NMEA_Gga_t Gga;
    ATT_APP_NMEA_Gga_t Zero;

    memset(&Zero, 0, sizeof(Zero));

    /* Bad arguments and bad framing */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGga(UT_GGA_FIX, strlen(UT_GGA_FIX), NULL), ATT_APP_NMEA_ERR_NULL);
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGga(NULL, 10, &Gga), ATT_APP_NMEA_ERR_NULL);
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGga("", 0, &Gga), ATT_APP_NMEA_ERR_LENGTH);

    /* Checksum failure leaves the output zeroed even though the text looks fine */
    memset(&Gga, 0xA5, sizeof(Gga));
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGga("$GPGGA,180000.00,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,*65",
                                            strlen(UT_GGA_FIX),
                                            &Gga),
                      ATT_APP_NMEA_ERR_CHECKSUM);
    UtAssert_MemCmp(&Gga, &Zero, sizeof(Gga), "Output zeroed after checksum failure");

    /* Truncated mid-sentence, as a partial read would produce */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGga(UT_GGA_FIX, 30, &Gga), ATT_APP_NMEA_ERR_FRAMING);

    /* Wrong sentence type, and an address of the wrong length */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGga(UT_GSV_3OF3, strlen(UT_GSV_3OF3), &Gga), ATT_APP_NMEA_ERR_TYPE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GGA,010203,0000.0000,N,00000.0000,E,1,04,,0,M,,M,,", &Gga), ATT_APP_NMEA_ERR_TYPE);

    /* Missing fields, extra fields, far too many fields */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD_COUNT);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD_COUNT);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,", &Gga), ATT_APP_NMEA_ERR_FIELD_COUNT);

    /* Time: empty, too short, not digits, out of range, malformed fraction */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,1800,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,18a000,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,240000,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,186000,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180061,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000x00,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.0x,3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);

    /* Fix quality: empty, not a number, out of range */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,A,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,9,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);

    /* Satellites used: empty, out of range, absurdly long */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,256,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,1234567890,0.9,1.4,M,,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);

    /* Latitude: empty with a fix, not a number, minutes >= 60, beyond the pole, bad hemisphere */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,39x4.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.39.82,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,-3944.3982,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3960.0000,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,9000.0001,N,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,E,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,NN,10459.4008,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);

    /* Longitude: empty with a fix, beyond 180, too many digits */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,18100.0000,W,1,10,0.9,1599.4,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,1045900000.4,W,1,10,0.9,1.4,M,,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008000000,W,1,10,0.9,1.4,M,,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);

    /* Altitude: empty with a fix, not a number, physically impossible */
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,10,0.9,,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,10,0.9,-,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,10,0.9,100000.1,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gga("GPGGA,180000.00,3944.3982,N,10459.4008,W,1,10,0.9,-100000.1,M,-18.0,M,,", &Gga),
                      ATT_APP_NMEA_ERR_RANGE);

    UtAssert_MemCmp(&Gga, &Zero, sizeof(Gga), "Output zeroed after a rejected sentence");
}

void Test_ATT_APP_NMEA_ParseGsv_Valid(void)
{
    /*
     * Test Case For:
     * ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGsv(const char *Sentence, size_t Length, ATT_APP_NMEA_Gsv_t *Gsv)
     */
    ATT_APP_NMEA_Gsv_t Gsv;

    /* Full sentence with four satellites */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGsv(UT_GSV_1OF3, strlen(UT_GSV_1OF3), &Gsv), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gsv.TotalMsgs, 3);
    UtAssert_UINT32_EQ(Gsv.MsgNum, 1);
    UtAssert_UINT32_EQ(Gsv.SatsInView, 10);
    UtAssert_UINT32_EQ(Gsv.NumSats, 4);
    UtAssert_UINT32_EQ(Gsv.Sats[0].Prn, 2);
    UtAssert_UINT32_EQ(Gsv.Sats[0].ElevationDeg, 62);
    UtAssert_UINT32_EQ(Gsv.Sats[0].AzimuthDeg, 45);
    UtAssert_UINT32_EQ(Gsv.Sats[0].Cno, 47);
    UtAssert_UINT32_EQ(Gsv.Sats[3].Prn, 9);
    UtAssert_UINT32_EQ(Gsv.Sats[3].AzimuthDeg, 210);
    UtAssert_UINT32_EQ(Gsv.Sats[3].Cno, 41);

    /* Last sentence of a group carries fewer satellites */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGsv(UT_GSV_3OF3, strlen(UT_GSV_3OF3), &Gsv), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gsv.MsgNum, 3);
    UtAssert_UINT32_EQ(Gsv.NumSats, 2);
    UtAssert_UINT32_EQ(Gsv.Sats[1].Prn, 30);
    UtAssert_UINT32_EQ(Gsv.Sats[1].Cno, 41);

    /* Empty C/N0 means "in view, not tracked"; the sentence is still good */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGsv(UT_GSV_EMPTY, strlen(UT_GSV_EMPTY), &Gsv), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gsv.NumSats, 4);
    UtAssert_UINT32_EQ(Gsv.Sats[0].Cno, 45);
    UtAssert_UINT32_EQ(Gsv.Sats[1].Prn, 5);
    UtAssert_UINT32_EQ(Gsv.Sats[1].Cno, ATT_APP_NMEA_CNO_NOT_TRACKED);
    UtAssert_UINT32_EQ(Gsv.Sats[3].Cno, ATT_APP_NMEA_CNO_NOT_TRACKED);

    /* Elevation and azimuth may be empty too (no almanac yet) */
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,,,33", &Gsv), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gsv.Sats[0].Prn, 17);
    UtAssert_UINT32_EQ(Gsv.Sats[0].ElevationDeg, 0);
    UtAssert_UINT32_EQ(Gsv.Sats[0].AzimuthDeg, 0);
    UtAssert_UINT32_EQ(Gsv.Sats[0].Cno, 33);

    /* No satellites in view at all */
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,00", &Gsv), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gsv.NumSats, 0);
    UtAssert_UINT32_EQ(Gsv.SatsInView, 0);

    /* NMEA 4.10 trailing signal ID, another talker, limits of each field */
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GLGSV,1,1,02,65,90,359,99,255,00,000,00,1", &Gsv), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Gsv.NumSats, 2);
    UtAssert_UINT32_EQ(Gsv.Sats[0].ElevationDeg, 90);
    UtAssert_UINT32_EQ(Gsv.Sats[0].AzimuthDeg, 359);
    UtAssert_UINT32_EQ(Gsv.Sats[0].Cno, 99);
    UtAssert_UINT32_EQ(Gsv.Sats[1].Prn, 255);
}

void Test_ATT_APP_NMEA_ParseGsv_Malformed(void)
{
    /*
     * Test Case For:
     * ATT_APP_NMEA_Status_t ATT_APP_NMEA_ParseGsv(const char *Sentence, size_t Length, ATT_APP_NMEA_Gsv_t *Gsv)
     */
    ATT_APP_NMEA_Gsv_t Gsv;
    ATT_APP_NMEA_Gsv_t Zero;

    memset(&Zero, 0, sizeof(Zero));

    /* Bad arguments, checksum failure, wrong type, partial sentence */
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGsv(UT_GSV_1OF3, strlen(UT_GSV_1OF3), NULL), ATT_APP_NMEA_ERR_NULL);
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGsv(NULL, 10, &Gsv), ATT_APP_NMEA_ERR_NULL);
    memset(&Gsv, 0xA5, sizeof(Gsv));
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGsv("$GPGSV,3,3,10,26,19,060,40,30,30,290,41*79", strlen(UT_GSV_3OF3), &Gsv),
                      ATT_APP_NMEA_ERR_CHECKSUM);
    UtAssert_MemCmp(&Gsv, &Zero, sizeof(Gsv), "Output zeroed after checksum failure");
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGsv(UT_GGA_FIX, strlen(UT_GGA_FIX), &Gsv), ATT_APP_NMEA_ERR_TYPE);
    UtAssert_INT32_EQ(ATT_APP_NMEA_ParseGsv(UT_GSV_1OF3, 25, &Gsv), ATT_APP_NMEA_ERR_FRAMING);

    /* Field count: header cut short, half a satellite, five satellites */
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1", &Gsv), ATT_APP_NMEA_ERR_FIELD_COUNT);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,45", &Gsv), ATT_APP_NMEA_ERR_FIELD_COUNT);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,45,120", &Gsv), ATT_APP_NMEA_ERR_FIELD_COUNT);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,05,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5", &Gsv),
                      ATT_APP_NMEA_ERR_FIELD_COUNT);

    /* Header numbers: empty, not digits, zero, out of order, out of range */
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,,1,01,17,45,120,33", &Gsv), ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,x,01,17,45,120,33", &Gsv), ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,0,1,01,17,45,120,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,0,01,17,45,120,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,2,3,01,17,45,120,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,10,1,01,17,45,120,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,,17,45,120,33", &Gsv), ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,256,17,45,120,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);

    /* Satellite ID: empty, zero, too large, not digits */
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,,45,120,33", &Gsv), ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,00,45,120,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,256,45,120,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,-7,45,120,33", &Gsv), ATT_APP_NMEA_ERR_FIELD);

    /* Elevation, azimuth and C/N0 out of range or not digits */
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,91,120,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,45,360,33", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,45,120,100", &Gsv), ATT_APP_NMEA_ERR_RANGE);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,4x,120,33", &Gsv), ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,45,1 0,33", &Gsv), ATT_APP_NMEA_ERR_FIELD);
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,01,17,45,120,3.3", &Gsv), ATT_APP_NMEA_ERR_FIELD);

    /* An error in the second satellite must not leave the first one behind */
    UtAssert_INT32_EQ(UT_NMEA_Gsv("GPGSV,1,1,02,17,45,120,33,18,45,120,xx", &Gsv), ATT_APP_NMEA_ERR_FIELD);
    UtAssert_MemCmp(&Gsv, &Zero, sizeof(Gsv), "Output zeroed after a rejected sentence");
}

void Test_ATT_APP_NMEA_Assembler_Epochs(void)
{
    /*
     * Test Case For:
     * ATT_APP_NMEA_AssemblerInit(), ATT_APP_NMEA_AssemblerFeed(), ATT_APP_NMEA_AssemblerFlush()
     *
     * A three sentence GSV group is merged into the epoch its GGA opened,
     * and the epoch is released when the next GGA arrives.
     */
    ATT_APP_NMEA_Assembler_t Assembler;
    ATT_APP_NMEA_Epoch_t     Epoch;
    bool                     Ready;

    memset(&Assembler, 0xA5, sizeof(Assembler));
    ATT_APP_NMEA_AssemblerInit(&Assembler);
    ATT_APP_NMEA_AssemblerInit(NULL); /* must not crash */
    UtAssert_True(!Assembler.HaveGga, "Assembler starts empty");

    /* Nothing to flush yet */
    UtAssert_True(!ATT_APP_NMEA_AssemblerFlush(&Assembler, &Epoch), "Flush with nothing collected");

    /* GSV before any GGA cannot be attributed to an epoch and is dropped */
    Ready = true;
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GSV_1OF3, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_True(!Ready, "No epoch from an orphan GSV");
    UtAssert_UINT32_EQ(Assembler.Pending.NumSats, 0);

    /* First epoch: GGA + 3 GSV, nothing is released yet */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GGA_FIX, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_True(!Ready, "First GGA only opens an epoch");
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GSV_1OF3, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GSV_2OF3, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GSV_3OF3, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_True(!Ready, "Epoch is not released until the next GGA");

    /* Sentences the app does not use are accepted and change nothing */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, "$GPRMC,1*56", &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_True(!Ready, "Unused sentence does not release an epoch");

    /* Second GGA releases the first epoch, complete */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GGA_NOFIX, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_True(Ready, "Second GGA releases the first epoch");
    UtAssert_UINT32_EQ(Epoch.Gga.FixQuality, 1);
    UtAssert_UINT32_EQ(Epoch.Gga.SatsUsed, 10);
    UtAssert_UINT32_EQ(Epoch.SatsInView, 10);
    UtAssert_UINT32_EQ(Epoch.NumSats, 10);
    UtAssert_UINT32_EQ(Epoch.Sats[0].Prn, 2);
    UtAssert_UINT32_EQ(Epoch.Sats[4].Prn, 13);
    UtAssert_UINT32_EQ(Epoch.Sats[9].Prn, 30);
    UtAssert_UINT32_EQ(Epoch.Sats[9].Cno, 41);

    /* The epoch now being collected is the no-fix one; flush hands it out */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GSV_EMPTY, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_True(ATT_APP_NMEA_AssemblerFlush(&Assembler, &Epoch), "Flush releases the pending epoch");
    UtAssert_UINT32_EQ(Epoch.Gga.FixQuality, ATT_APP_NMEA_FIX_QUALITY_NONE);
    UtAssert_UINT32_EQ(Epoch.NumSats, 4);
    UtAssert_UINT32_EQ(Epoch.Sats[1].Cno, ATT_APP_NMEA_CNO_NOT_TRACKED);

    /* Flush leaves the assembler empty */
    UtAssert_True(!ATT_APP_NMEA_AssemblerFlush(&Assembler, &Epoch), "Second flush has nothing");
    UtAssert_True(!ATT_APP_NMEA_AssemblerFlush(NULL, &Epoch), "Flush tolerates NULL assembler");

    /* Flush without an output still clears the assembler */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GGA_FIX, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_True(!ATT_APP_NMEA_AssemblerFlush(&Assembler, NULL), "Flush tolerates NULL output");
    UtAssert_True(!Assembler.HaveGga, "Assembler cleared");
}

void Test_ATT_APP_NMEA_Assembler_Errors(void)
{
    /*
     * Test Case For:
     * ATT_APP_NMEA_AssemblerFeed()
     *
     * Rejected sentences are reported and never damage the epoch in progress.
     */
    ATT_APP_NMEA_Assembler_t Assembler;
    ATT_APP_NMEA_Epoch_t     Epoch;
    bool                     Ready;
    char                     Buf[2 * ATT_APP_NMEA_MAX_SENTENCE_LEN];
    uint8                    i;

    ATT_APP_NMEA_AssemblerInit(&Assembler);

    /* NULL arguments */
    UtAssert_INT32_EQ(ATT_APP_NMEA_AssemblerFeed(NULL, UT_GGA_FIX, strlen(UT_GGA_FIX), &Epoch, &Ready),
                      ATT_APP_NMEA_ERR_NULL);
    UtAssert_INT32_EQ(ATT_APP_NMEA_AssemblerFeed(&Assembler, NULL, 5, &Epoch, &Ready), ATT_APP_NMEA_ERR_NULL);
    UtAssert_INT32_EQ(ATT_APP_NMEA_AssemblerFeed(&Assembler, UT_GGA_FIX, strlen(UT_GGA_FIX), NULL, &Ready),
                      ATT_APP_NMEA_ERR_NULL);
    UtAssert_INT32_EQ(ATT_APP_NMEA_AssemblerFeed(&Assembler, UT_GGA_FIX, strlen(UT_GGA_FIX), &Epoch, NULL),
                      ATT_APP_NMEA_ERR_NULL);

    /* Garbage, empty lines and a corrupted "other" sentence are all reported */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, "", &Epoch, &Ready), ATT_APP_NMEA_ERR_LENGTH);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, "hello world, this is not NMEA", &Epoch, &Ready),
                      ATT_APP_NMEA_ERR_FRAMING);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, "$GPRMC,1*5C", &Epoch, &Ready), ATT_APP_NMEA_ERR_CHECKSUM);

    /* Well framed and checksummed, but the address field is not 5 characters */
    UT_NMEA_Make(Buf, sizeof(Buf), "GPGGAX,1,2,3");
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, Buf, &Epoch, &Ready), ATT_APP_NMEA_ERR_FRAMING);

    /* A corrupted GGA does not release or replace the epoch in progress */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GGA_FIX, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GSV_1OF3, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, "$GPGGA,180352.00,,,,,0,03,,,M,,M,,*47", &Epoch, &Ready),
                      ATT_APP_NMEA_ERR_CHECKSUM);
    UtAssert_True(!Ready, "Corrupted GGA releases nothing");
    UtAssert_UINT32_EQ(Assembler.Pending.Gga.FixQuality, 1);
    UtAssert_UINT32_EQ(Assembler.Pending.NumSats, 4);

    /* Sentence 2 of 3 was lost: sentence 3 is out of sequence and is not merged */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GSV_3OF3, &Epoch, &Ready), ATT_APP_NMEA_ERR_SEQUENCE);
    UtAssert_UINT32_EQ(Assembler.Pending.NumSats, 4);

    /* A corrupted GSV is rejected by the parser before it reaches the epoch */
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, "$GPGSV,3,3,10,26,19,060,40,30,30,290,41*00", &Epoch, &Ready),
                      ATT_APP_NMEA_ERR_CHECKSUM);
    UtAssert_UINT32_EQ(Assembler.Pending.NumSats, 4);

    /* Group total changing mid-group is also a sequence error */
    ATT_APP_NMEA_AssemblerInit(&Assembler);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GGA_FIX, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GSV_1OF3, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UT_NMEA_Make(Buf, sizeof(Buf), "GPGSV,4,2,10,13,35,088,41");
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, Buf, &Epoch, &Ready), ATT_APP_NMEA_ERR_SEQUENCE);

    /*
     * More satellites than the epoch can hold: two talkers, 4 sentences of 4.
     * The extra satellites are dropped without error and without overrun,
     * while SatsInView keeps the receiver's own total.
     */
    ATT_APP_NMEA_AssemblerInit(&Assembler);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GGA_FIX, &Epoch, &Ready), ATT_APP_NMEA_OK);
    for (i = 0; i < 2; ++i)
    {
        UT_NMEA_Make(Buf, sizeof(Buf), "GPGSV,2,1,08,01,10,010,40,02,10,020,40,03,10,030,40,04,10,040,40");
        UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, Buf, &Epoch, &Ready), ATT_APP_NMEA_OK);
        UT_NMEA_Make(Buf, sizeof(Buf), "GPGSV,2,2,08,05,10,050,40,06,10,060,40,07,10,070,40,08,10,080,40");
        UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, Buf, &Epoch, &Ready), ATT_APP_NMEA_OK);
    }
    UtAssert_True(ATT_APP_NMEA_AssemblerFlush(&Assembler, &Epoch), "Flush oversized epoch");
    UtAssert_UINT32_EQ(Epoch.NumSats, ATT_APP_MISSION_MAX_SATS);
    UtAssert_UINT32_EQ(Epoch.SatsInView, 16);

    /* SatsInView saturates instead of wrapping when many groups are summed */
    ATT_APP_NMEA_AssemblerInit(&Assembler);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, UT_GGA_FIX, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UT_NMEA_Make(Buf, sizeof(Buf), "GPGSV,1,1,200");
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, Buf, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_INT32_EQ(UT_NMEA_Feed(&Assembler, Buf, &Epoch, &Ready), ATT_APP_NMEA_OK);
    UtAssert_UINT32_EQ(Assembler.Pending.SatsInView, 255);
}

/*
 * Register the test cases to execute with the unit test tool
 */
void UtTest_Setup(void)
{
    ADD_TEST(ATT_APP_NMEA_VerifyChecksum);
    ADD_TEST(ATT_APP_NMEA_GetSentenceType);
    ADD_TEST(ATT_APP_NMEA_ParseGga_Valid);
    ADD_TEST(ATT_APP_NMEA_ParseGga_Malformed);
    ADD_TEST(ATT_APP_NMEA_ParseGsv_Valid);
    ADD_TEST(ATT_APP_NMEA_ParseGsv_Malformed);
    ADD_TEST(ATT_APP_NMEA_Assembler_Epochs);
    ADD_TEST(ATT_APP_NMEA_Assembler_Errors);
}
