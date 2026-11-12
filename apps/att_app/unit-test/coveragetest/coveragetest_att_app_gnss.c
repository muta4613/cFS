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
** File: coveragetest_att_app_gnss.c
**
** Purpose:
** Coverage Unit Test cases for the ATT_APP GNSS input handling.
**
** Notes:
** The unit under test sits between OSAL (file access) and the NMEA
** assembler. Both are replaced by scripted handlers here: one plays back
** a list of read results, the other decides which sentence completes an
** epoch. The tests then check what was read, what was passed on, and
** what was published.
*/

/*
 * Includes
 */
#include "att_app_coveragetest_common.h"
#include "att_app_gnss.h"
#include "att_app_nmea.h"

/* Most scripted steps one test needs */
#define UT_GNSS_MAX_STEPS 80

/* Size of the text kept from each line handed to the assembler */
#define UT_GNSS_LINE_TEXT_SIZE 32

/*
 * Script for the OS_TimedRead handler: each call hands out the next
 * chunk; once the chunks run out every call returns FinalStatus.
 */
typedef struct
{
    const char *Chunks[UT_GNSS_MAX_STEPS];
    uint32      NumChunks;
    uint32      NextChunk;
    int32       FinalStatus;
    uint32      CallCount;
} UT_GNSS_ReadScript_t;

/*
 * Script for the ATT_APP_NMEA_AssemblerFeed handler: per call, the status
 * to return and whether that call completes an epoch. Calls beyond the
 * script return OK / not ready. Every line passed in is recorded.
 */
typedef struct
{
    ATT_APP_NMEA_Status_t Status[UT_GNSS_MAX_STEPS];
    bool                  Ready[UT_GNSS_MAX_STEPS];
    ATT_APP_NMEA_Epoch_t  Epoch;
    uint32                CallCount;
    size_t                LineLength[UT_GNSS_MAX_STEPS];
    char                  LineText[UT_GNSS_MAX_STEPS][UT_GNSS_LINE_TEXT_SIZE];
} UT_GNSS_FeedScript_t;

/* What ATT_APP_NMEA_AssemblerFlush should hand out */
typedef struct
{
    bool                 HaveEpoch;
    ATT_APP_NMEA_Epoch_t Epoch;
} UT_GNSS_FlushScript_t;

static UT_GNSS_ReadScript_t  UT_GNSS_Read;
static UT_GNSS_FeedScript_t  UT_GNSS_Feed;
static UT_GNSS_FlushScript_t UT_GNSS_Flush;

static void UT_GNSS_ReadHandler(void *UserObj, UT_EntryKey_t FuncKey, const UT_StubContext_t *Context)
{
    UT_GNSS_ReadScript_t *Script = UserObj;
    void                 *Buffer = UT_Hook_GetArgValueByName(Context, "buffer", void *);
    size_t                NBytes = UT_Hook_GetArgValueByName(Context, "nbytes", size_t);
    int32                 Status = Script->FinalStatus;
    size_t                Length;

    ++Script->CallCount;

    if (Script->NextChunk < Script->NumChunks)
    {
        Length = strlen(Script->Chunks[Script->NextChunk]);

        /* The unit under test must never be handed more than it asked for */
        UtAssert_True(Length <= NBytes,
                      "Read chunk %u (%u bytes) fits the %u bytes requested",
                      (unsigned int)Script->NextChunk,
                      (unsigned int)Length,
                      (unsigned int)NBytes);

        memcpy(Buffer, Script->Chunks[Script->NextChunk], Length);
        Status = (int32)Length;
        ++Script->NextChunk;
    }

    UT_Stub_SetReturnValue(FuncKey, Status);
}

static void UT_GNSS_FeedHandler(void *UserObj, UT_EntryKey_t FuncKey, const UT_StubContext_t *Context)
{
    UT_GNSS_FeedScript_t *Script     = UserObj;
    const char           *Sentence   = UT_Hook_GetArgValueByName(Context, "Sentence", const char *);
    size_t                Length     = UT_Hook_GetArgValueByName(Context, "Length", size_t);
    ATT_APP_NMEA_Epoch_t *Epoch      = UT_Hook_GetArgValueByName(Context, "Epoch", ATT_APP_NMEA_Epoch_t *);
    bool                 *EpochReady = UT_Hook_GetArgValueByName(Context, "EpochReady", bool *);
    ATT_APP_NMEA_Status_t Status     = ATT_APP_NMEA_OK;
    uint32                Call       = Script->CallCount;
    size_t                CopyLength;

    *EpochReady = false;

    if (Call < UT_GNSS_MAX_STEPS)
    {
        CopyLength = (Length < UT_GNSS_LINE_TEXT_SIZE - 1) ? Length : UT_GNSS_LINE_TEXT_SIZE - 1;
        memcpy(Script->LineText[Call], Sentence, CopyLength);
        Script->LineText[Call][CopyLength] = '\0';
        Script->LineLength[Call]           = Length;

        Status = Script->Status[Call];
        if (Script->Ready[Call])
        {
            *Epoch      = Script->Epoch;
            *EpochReady = true;
        }
    }

    ++Script->CallCount;

    UT_Stub_SetReturnValue(FuncKey, Status);
}

static void UT_GNSS_FlushHandler(void *UserObj, UT_EntryKey_t FuncKey, const UT_StubContext_t *Context)
{
    UT_GNSS_FlushScript_t *Script = UserObj;
    ATT_APP_NMEA_Epoch_t  *Epoch  = UT_Hook_GetArgValueByName(Context, "Epoch", ATT_APP_NMEA_Epoch_t *);
    bool                   Result = Script->HaveEpoch;

    if (Result)
    {
        *Epoch = Script->Epoch;
    }

    UT_Stub_SetReturnValue(FuncKey, Result);
}

/* An epoch with a fix, three tracked satellites and one untracked */
static void UT_GNSS_MakeEpoch(ATT_APP_NMEA_Epoch_t *Epoch)
{
    memset(Epoch, 0, sizeof(*Epoch));

    Epoch->Gga.Latitude       = 39.74;
    Epoch->Gga.Longitude      = -104.99;
    Epoch->Gga.Altitude       = 1600.5f;
    Epoch->Gga.UtcHour        = 18;
    Epoch->Gga.UtcMinute      = 2;
    Epoch->Gga.UtcSecond      = 35;
    Epoch->Gga.UtcCentisecond = 50;
    Epoch->Gga.FixQuality     = 1;
    Epoch->Gga.SatsUsed       = 3;
    Epoch->SatsInView         = 4;
    Epoch->NumSats            = 4;
    Epoch->Sats[0].Prn        = 2;
    Epoch->Sats[0].Cno        = 40;
    Epoch->Sats[1].Prn        = 5;
    Epoch->Sats[1].Cno        = ATT_APP_NMEA_CNO_NOT_TRACKED;
    Epoch->Sats[2].Prn        = 7;
    Epoch->Sats[2].Cno        = 44;
    Epoch->Sats[3].Prn        = 9;
    Epoch->Sats[3].Cno        = 39;
}

/*
 * Common start for every test: scripted handlers attached and the input
 * opened in the requested mode.
 */
static void UT_GNSS_Start(uint8 InputMode)
{
    memset(&UT_GNSS_Read, 0, sizeof(UT_GNSS_Read));
    memset(&UT_GNSS_Feed, 0, sizeof(UT_GNSS_Feed));
    memset(&UT_GNSS_Flush, 0, sizeof(UT_GNSS_Flush));
    memset(&ATT_APP_Data, 0, sizeof(ATT_APP_Data));

    UT_GNSS_MakeEpoch(&UT_GNSS_Feed.Epoch);
    UT_GNSS_MakeEpoch(&UT_GNSS_Flush.Epoch);

    UT_SetHandlerFunction(UT_KEY(OS_TimedRead), UT_GNSS_ReadHandler, &UT_GNSS_Read);
    UT_SetHandlerFunction(UT_KEY(ATT_APP_NMEA_AssemblerFeed), UT_GNSS_FeedHandler, &UT_GNSS_Feed);
    UT_SetHandlerFunction(UT_KEY(ATT_APP_NMEA_AssemblerFlush), UT_GNSS_FlushHandler, &UT_GNSS_Flush);

    UtAssert_INT32_EQ(ATT_APP_GNSS_Init(InputMode), CFE_SUCCESS);
}

/* Queue the next chunk the read handler will return */
static void UT_GNSS_AddChunk(const char *Text)
{
    UT_GNSS_Read.Chunks[UT_GNSS_Read.NumChunks] = Text;
    ++UT_GNSS_Read.NumChunks;
}

/*
**********************************************************************************
**          TEST CASE FUNCTIONS
**********************************************************************************
*/

void Test_ATT_APP_GNSS_Init(void)
{
    /*
     * Test Case For:
     * CFE_Status_t ATT_APP_GNSS_Init(uint8 InputMode)
     */
    UT_CheckEvent_t EventTest;

    /* File mode, input opens: one information event, no device mapping */
    memset(&ATT_APP_Data, 0xA5, sizeof(ATT_APP_Data));
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_GNSS_OPEN_INF_EID, NULL);
    UtAssert_INT32_EQ(ATT_APP_GNSS_Init(ATT_APP_GNSS_INPUT_MODE_FILE), CFE_SUCCESS);
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
    UtAssert_True(ATT_APP_Data.Gnss.IsOpen, "Input open");
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.InputMode, ATT_APP_GNSS_INPUT_MODE_FILE);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.EpochCounter, 0);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.ParseErrorCounter, 0);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.BufTail, 0);
    UtAssert_STUB_COUNT(CFE_MSG_Init, 1);
    UtAssert_STUB_COUNT(OS_OpenCreate, 1);
    UtAssert_STUB_COUNT(OS_FileSysAddFixedMap, 0);
    UtAssert_STUB_COUNT(ATT_APP_NMEA_AssemblerInit, 1);

    /* Missing file: an error event, but initialization still succeeds so the app keeps running */
    UT_ResetState(0);
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_GNSS_OPEN_ERR_EID, NULL);
    UT_SetDeferredRetcode(UT_KEY(OS_OpenCreate), 1, OS_ERROR);
    UtAssert_INT32_EQ(ATT_APP_GNSS_Init(ATT_APP_GNSS_INPUT_MODE_FILE), CFE_SUCCESS);
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
    UtAssert_True(!ATT_APP_Data.Gnss.IsOpen, "Input not open");
    UtAssert_True(!OS_ObjectIdDefined(ATT_APP_Data.Gnss.FileId), "No file handle kept");

    /* With nothing open a tick does nothing at all */
    ATT_APP_GNSS_Tick();
    UtAssert_STUB_COUNT(OS_TimedRead, 0);
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 0);

    /* Device mode maps the device directory before opening */
    UT_ResetState(0);
    UtAssert_INT32_EQ(ATT_APP_GNSS_Init(ATT_APP_GNSS_INPUT_MODE_DEVICE), CFE_SUCCESS);
    UtAssert_STUB_COUNT(OS_FileSysAddFixedMap, 1);
    UtAssert_STUB_COUNT(OS_OpenCreate, 1);
    UtAssert_True(ATT_APP_Data.Gnss.IsOpen, "Device open");

    /* Mapping failure is reported; the open is still attempted (and reports its own failure) */
    UT_ResetState(0);
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_GNSS_DEVMAP_ERR_EID, NULL);
    UT_SetDeferredRetcode(UT_KEY(OS_FileSysAddFixedMap), 1, OS_ERROR);
    UT_SetDeferredRetcode(UT_KEY(OS_OpenCreate), 1, OS_FS_ERR_PATH_INVALID);
    UtAssert_INT32_EQ(ATT_APP_GNSS_Init(ATT_APP_GNSS_INPUT_MODE_DEVICE), CFE_SUCCESS);
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
    UtAssert_STUB_COUNT(CFE_EVS_SendEvent, 2);
    UtAssert_True(!ATT_APP_Data.Gnss.IsOpen, "Device not open");
}

void Test_ATT_APP_GNSS_Restart(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Restart(void)
     */
    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);

    /* Leave some state behind, as if the replay was part way through */
    ATT_APP_Data.Gnss.BufHead        = 10;
    ATT_APP_Data.Gnss.BufTail        = 20;
    ATT_APP_Data.Gnss.DiscardingLine = true;
    ATT_APP_Data.Gnss.LinesThisPass  = 7;
    ATT_APP_Data.Gnss.EpochCounter   = 3;

    /* Open input: closed, cleared and opened again; counters are telemetry and are kept */
    ATT_APP_GNSS_Restart();
    UtAssert_STUB_COUNT(OS_close, 1);
    UtAssert_STUB_COUNT(OS_OpenCreate, 2);
    UtAssert_True(ATT_APP_Data.Gnss.IsOpen, "Input open again");
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.BufHead, 0);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.BufTail, 0);
    UtAssert_True(!ATT_APP_Data.Gnss.DiscardingLine, "Discard state cleared");
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.LinesThisPass, 0);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.EpochCounter, 3);

    /* Restart that fails to open leaves the input closed... */
    UT_SetDeferredRetcode(UT_KEY(OS_OpenCreate), 1, OS_ERROR);
    ATT_APP_GNSS_Restart();
    UtAssert_STUB_COUNT(OS_close, 2);
    UtAssert_True(!ATT_APP_Data.Gnss.IsOpen, "Input closed after failed restart");

    /* ...and the next restart must not close a handle that is not open */
    ATT_APP_GNSS_Restart();
    UtAssert_STUB_COUNT(OS_close, 2);
    UtAssert_True(ATT_APP_Data.Gnss.IsOpen, "Input recovered");
}

void Test_ATT_APP_GNSS_Tick_FileEpoch(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Tick(void)
     *
     * Replay of a log: exactly one epoch is published per tick, sentences
     * may be split across reads, and the NAV packet carries the epoch.
     */
    CFE_MSG_Message_t        *MsgSend      = NULL;
    CFE_MSG_Message_t        *MsgTimestamp = NULL;
    ATT_APP_NavTlm_Payload_t *Nav          = &ATT_APP_Data.NavTlm.Payload;

    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);

    /* Two reads; the second sentence is cut in half by the read boundary */
    UT_GNSS_AddChunk("$GGA1\r\n$GS");
    UT_GNSS_AddChunk("V1\r\n$GGA2\r\n$GSV2\r\n");
    UT_GNSS_Feed.Ready[2] = true; /* third sentence (the next GGA) completes the first epoch */

    UT_SetDataBuffer(UT_KEY(CFE_SB_TransmitMsg), &MsgSend, sizeof(MsgSend), false);
    UT_SetDataBuffer(UT_KEY(CFE_SB_TimeStampMsg), &MsgTimestamp, sizeof(MsgTimestamp), false);

    ATT_APP_GNSS_Tick();

    /* Whole sentences were passed on, without the line feed */
    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, 3);
    UtAssert_StrCmp(UT_GNSS_Feed.LineText[0], "$GGA1\r", "First sentence");
    UtAssert_StrCmp(UT_GNSS_Feed.LineText[1], "$GSV1\r", "Second sentence, joined across two reads");
    UtAssert_StrCmp(UT_GNSS_Feed.LineText[2], "$GGA2\r", "Third sentence");
    UtAssert_UINT32_EQ(UT_GNSS_Read.CallCount, 2);

    /* One NAV packet, time stamped and sent */
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 1);
    UtAssert_STUB_COUNT(CFE_SB_TimeStampMsg, 1);
    UtAssert_ADDRESS_EQ(MsgSend, &ATT_APP_Data.NavTlm);
    UtAssert_ADDRESS_EQ(MsgTimestamp, &ATT_APP_Data.NavTlm);

    /* Contents come from the epoch */
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.EpochCounter, 1);
    UtAssert_UINT32_EQ(Nav->EpochCounter, 1);
    UtAssert_UINT32_EQ(Nav->ParseErrorCounter, 0);
    UtAssert_True(Nav->Latitude == 39.74 && Nav->Longitude == -104.99, "Position copied");
    UtAssert_True(Nav->Altitude == 1600.5f, "Altitude copied");
    UtAssert_UINT32_EQ(Nav->UtcHour, 18);
    UtAssert_UINT32_EQ(Nav->UtcMinute, 2);
    UtAssert_UINT32_EQ(Nav->UtcSecond, 35);
    UtAssert_UINT32_EQ(Nav->UtcCentisecond, 50);
    UtAssert_UINT32_EQ(Nav->FixQuality, 1);
    UtAssert_UINT32_EQ(Nav->SatsUsed, 3);
    UtAssert_UINT32_EQ(Nav->SatsInView, 4);
    UtAssert_UINT32_EQ(Nav->Sats[0].Prn, 2);
    UtAssert_UINT32_EQ(Nav->Sats[0].Cno, 40);
    UtAssert_UINT32_EQ(Nav->Sats[1].Prn, 5);
    UtAssert_UINT32_EQ(Nav->Sats[1].Cno, 0);
    UtAssert_UINT32_EQ(Nav->Sats[3].Prn, 9);
    UtAssert_UINT32_EQ(Nav->Sats[4].Prn, 0);

    /* Mean is over the three tracked satellites only: (40 + 44 + 39) / 3 */
    UtAssert_True(Nav->MeanCno == 41.0f, "MeanCno %.2f", (double)Nav->MeanCno);

    /* Second tick: the sentence left in the buffer is used before anything is read */
    UT_GNSS_AddChunk("$GGA3\r\n");
    UT_GNSS_Feed.Ready[4] = true;
    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, 5);
    UtAssert_StrCmp(UT_GNSS_Feed.LineText[3], "$GSV2\r", "Buffered sentence");
    UtAssert_StrCmp(UT_GNSS_Feed.LineText[4], "$GGA3\r", "Newly read sentence");
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 2);
    UtAssert_UINT32_EQ(Nav->EpochCounter, 2);
}

void Test_ATT_APP_GNSS_Tick_NoTrackedSats(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Tick(void)
     *
     * An epoch in which nothing is tracked must not divide by zero.
     */
    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);

    UT_GNSS_Feed.Epoch.Sats[0].Cno = ATT_APP_NMEA_CNO_NOT_TRACKED;
    UT_GNSS_Feed.Epoch.Sats[2].Cno = ATT_APP_NMEA_CNO_NOT_TRACKED;
    UT_GNSS_Feed.Epoch.Sats[3].Cno = ATT_APP_NMEA_CNO_NOT_TRACKED;
    UT_GNSS_Feed.Ready[0]          = true;
    UT_GNSS_AddChunk("$GGA\n");

    ATT_APP_GNSS_Tick();

    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 1);
    UtAssert_True(ATT_APP_Data.NavTlm.Payload.MeanCno == 0.0f, "MeanCno is 0 with nothing tracked");
}

void Test_ATT_APP_GNSS_Tick_ParseErrors(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Tick(void)
     *
     * Rejected sentences are counted and reported, and do not stop the epoch.
     */
    UT_CheckEvent_t EventTest;

    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_GNSS_PARSE_ERR_EID, NULL);

    UT_GNSS_AddChunk("bad1\nbad2\n$GGA\n");
    UT_GNSS_Feed.Status[0] = ATT_APP_NMEA_ERR_CHECKSUM;
    UT_GNSS_Feed.Status[1] = ATT_APP_NMEA_ERR_FRAMING;
    UT_GNSS_Feed.Ready[2]  = true;

    ATT_APP_GNSS_Tick();

    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.ParseErrorCounter, 2);
    UtAssert_UINT32_EQ(EventTest.MatchCount, 2);
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 1);
    UtAssert_UINT32_EQ(ATT_APP_Data.NavTlm.Payload.ParseErrorCounter, 2);
}

void Test_ATT_APP_GNSS_Tick_FileWrap(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Tick(void)
     *
     * End of the log: the epoch still being collected is published, and
     * the replay starts again from the first byte.
     */
    UT_CheckEvent_t EventTest;

    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_GNSS_WRAP_INF_EID, NULL);

    /* Last sentence of the file has no line feed; it must still be parsed */
    UT_GNSS_AddChunk("$GGA\r\n$GSV-last");
    UT_GNSS_Read.FinalStatus = 0; /* end of file */
    UT_GNSS_Flush.HaveEpoch  = true;

    ATT_APP_GNSS_Tick();

    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, 2);
    UtAssert_StrCmp(UT_GNSS_Feed.LineText[1], "$GSV-last", "Unterminated final sentence");
    UtAssert_STUB_COUNT(ATT_APP_NMEA_AssemblerFlush, 1);
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 1);
    UtAssert_STUB_COUNT(OS_lseek, 1);
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
    UtAssert_True(ATT_APP_Data.Gnss.IsOpen, "Input stays open across the wrap");
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.BufTail, 0);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.LinesThisPass, 0);

    /* File that ends cleanly on a line feed, with nothing pending in the assembler */
    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);
    UT_ResetState(UT_KEY(CFE_SB_TransmitMsg));
    UT_ResetState(UT_KEY(OS_lseek));
    UT_GNSS_AddChunk("$GSV\r\n");
    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, 1);
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 0);
    UtAssert_STUB_COUNT(OS_lseek, 1);
    UtAssert_True(ATT_APP_Data.Gnss.IsOpen, "Input stays open");
}

void Test_ATT_APP_GNSS_Tick_FileFaults(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Tick(void)
     *
     * Empty log, rewind failure and read failure each close the input with
     * one error event, so the fault is not repeated every second.
     */
    UT_CheckEvent_t EventTest;

    /* Empty file: wrapping would only hit end of file again one second later, forever */
    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_GNSS_READ_ERR_EID, NULL);
    UT_GNSS_Read.FinalStatus = 0;
    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
    UtAssert_True(!ATT_APP_Data.Gnss.IsOpen, "Empty log closes the input");
    UtAssert_STUB_COUNT(OS_lseek, 0);
    UtAssert_STUB_COUNT(OS_close, 1);

    /* Later ticks stay quiet */
    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
    UtAssert_UINT32_EQ(UT_GNSS_Read.CallCount, 1);

    /* Rewind fails at end of file */
    UT_ResetState(0);
    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_GNSS_READ_ERR_EID, NULL);
    UT_GNSS_AddChunk("$GGA\n");
    UT_GNSS_Read.FinalStatus = 0;
    UT_SetDeferredRetcode(UT_KEY(OS_lseek), 1, OS_ERROR);
    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
    UtAssert_True(!ATT_APP_Data.Gnss.IsOpen, "Rewind failure closes the input");

    /* Read fails outright */
    UT_ResetState(0);
    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_GNSS_READ_ERR_EID, NULL);
    UT_GNSS_Read.FinalStatus = OS_ERROR;
    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
    UtAssert_True(!ATT_APP_Data.Gnss.IsOpen, "Read failure closes the input");
    UtAssert_STUB_COUNT(OS_close, 1);
    UtAssert_STUB_COUNT(ATT_APP_NMEA_AssemblerFlush, 0);
}

void Test_ATT_APP_GNSS_Tick_Device(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Tick(void)
     *
     * Live receiver: everything that has arrived is processed, "no data
     * yet" is normal, and nothing is ever rewound.
     */
    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_DEVICE);

    /* A backlog holding two complete epochs: both are published in one tick */
    UT_GNSS_AddChunk("$GGA1\n$GGA2\n$GGA3\n$GS");
    UT_GNSS_Feed.Ready[1]    = true;
    UT_GNSS_Feed.Ready[2]    = true;
    UT_GNSS_Read.FinalStatus = OS_ERROR_TIMEOUT; /* nothing more waiting */

    ATT_APP_GNSS_Tick();

    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, 3);
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 2);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.EpochCounter, 2);
    UtAssert_True(ATT_APP_Data.Gnss.IsOpen, "Timeout leaves the device open");
    UtAssert_STUB_COUNT(OS_lseek, 0);
    UtAssert_STUB_COUNT(ATT_APP_NMEA_AssemblerFlush, 0);
    UtAssert_STUB_COUNT(CFE_EVS_SendEvent, 1); /* only the "opened" event from start-up */

    /* Half a sentence is waiting; the rest arrives by the next tick */
    UT_GNSS_AddChunk("V3\n");
    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, 4);
    UtAssert_StrCmp(UT_GNSS_Feed.LineText[3], "$GSV3", "Sentence completed by the next tick");

    /* A zero-length read from a device is not end of file to wrap on */
    UT_GNSS_Read.FinalStatus = 0;
    ATT_APP_GNSS_Tick();
    UtAssert_True(ATT_APP_Data.Gnss.IsOpen, "Zero-length read leaves the device open");
    UtAssert_STUB_COUNT(OS_lseek, 0);

    /* A real read error still closes it */
    UT_GNSS_Read.FinalStatus = OS_ERROR;
    ATT_APP_GNSS_Tick();
    UtAssert_True(!ATT_APP_Data.Gnss.IsOpen, "Read error closes the device");
}

void Test_ATT_APP_GNSS_Tick_LineLimit(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Tick(void)
     *
     * A device that floods the input cannot hold the task: one tick
     * handles a bounded number of lines and leaves the rest for the next.
     */
    static char Flood[ATT_APP_PLATFORM_GNSS_MAX_LINES_PER_TICK + 9];
    uint32      i;

    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_DEVICE);

    /* Empty lines are the densest possible input: one line per byte */
    memset(Flood, '\n', sizeof(Flood) - 1);
    Flood[sizeof(Flood) - 1] = '\0';
    UT_GNSS_AddChunk(Flood);
    UT_GNSS_Read.FinalStatus = OS_ERROR_TIMEOUT;

    for (i = 0; i < UT_GNSS_MAX_STEPS; ++i)
    {
        UT_GNSS_Feed.Status[i] = ATT_APP_NMEA_ERR_LENGTH;
    }

    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, ATT_APP_PLATFORM_GNSS_MAX_LINES_PER_TICK);

    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, sizeof(Flood) - 1);
}

void Test_ATT_APP_GNSS_Tick_OverlongLine(void)
{
    /*
     * Test Case For:
     * void ATT_APP_GNSS_Tick(void)
     *
     * A "line" longer than the whole buffer (noise with no line feed) is
     * thrown away without overrunning the buffer, counted once, and the
     * stream recovers at the next line feed.
     */
    static char Full[ATT_APP_PLATFORM_GNSS_READ_BUF_SIZE + 1];

    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_DEVICE);

    memset(Full, 'x', sizeof(Full) - 1);
    Full[sizeof(Full) - 1] = '\0';

    /* Two buffers full of noise, then its tail, then a good sentence */
    UT_GNSS_AddChunk(Full);
    UT_GNSS_AddChunk(Full);
    UT_GNSS_AddChunk("tail-of-noise\n$GGA\n");
    UT_GNSS_Read.FinalStatus = OS_ERROR_TIMEOUT;

    ATT_APP_GNSS_Tick();

    /* Counted once although it spanned two buffers; none of the noise reached the parser */
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.ParseErrorCounter, 1);
    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, 1);
    UtAssert_StrCmp(UT_GNSS_Feed.LineText[0], "$GGA", "First sentence after the noise");
    UtAssert_True(!ATT_APP_Data.Gnss.DiscardingLine, "Discard state cleared at the line feed");

    /* In a file, noise that runs to end of file is dropped too, not parsed as a last sentence */
    UT_ResetState(0);
    UT_GNSS_Start(ATT_APP_GNSS_INPUT_MODE_FILE);
    UT_GNSS_AddChunk("$GGA\n");
    UT_GNSS_AddChunk(Full);
    UT_GNSS_AddChunk("still-noise");
    UT_GNSS_Read.FinalStatus = 0;
    ATT_APP_GNSS_Tick();
    UtAssert_UINT32_EQ(UT_GNSS_Feed.CallCount, 1);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.ParseErrorCounter, 1);
    UtAssert_STUB_COUNT(OS_lseek, 1);
}

/*
 * Register the test cases to execute with the unit test tool
 */
void UtTest_Setup(void)
{
    ADD_TEST(ATT_APP_GNSS_Init);
    ADD_TEST(ATT_APP_GNSS_Restart);
    ADD_TEST(ATT_APP_GNSS_Tick_FileEpoch);
    ADD_TEST(ATT_APP_GNSS_Tick_NoTrackedSats);
    ADD_TEST(ATT_APP_GNSS_Tick_ParseErrors);
    ADD_TEST(ATT_APP_GNSS_Tick_FileWrap);
    ADD_TEST(ATT_APP_GNSS_Tick_FileFaults);
    ADD_TEST(ATT_APP_GNSS_Tick_Device);
    ADD_TEST(ATT_APP_GNSS_Tick_LineLimit);
    ADD_TEST(ATT_APP_GNSS_Tick_OverlongLine);
}
