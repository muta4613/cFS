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

/**
 * \file
 *   GNSS input handling: OSAL file/device access, line framing, and
 *   publication of the NAV telemetry packet.
 */

#include "att_app.h"
#include "att_app_gnss.h"
#include "att_app_eventids.h"
#include "att_app_msgids.h"
#include "att_app_msg.h"

#include <string.h>

/************************************************************************
 * Local Macro Definitions
 ************************************************************************/

#define ATT_APP_GNSS_LINE_TERMINATOR '\n'

/* Full OSAL path of the receiver's device node in device mode */
#define ATT_APP_GNSS_DEVICE_PATH ATT_APP_PLATFORM_GNSS_DEVICE_MOUNT "/" ATT_APP_PLATFORM_GNSS_DEVICE_NAME

/* Events report dB values as whole and tenths, because not every target's printf supports %f */
#define ATT_APP_GNSS_TENTHS_PER_DB 10

/* The buffer must always be able to hold a complete sentence plus the start of the next */
CompileTimeAssert(ATT_APP_PLATFORM_GNSS_READ_BUF_SIZE >= 2 * (ATT_APP_NMEA_MAX_SENTENCE_LEN + 2),
                  ATT_APP_GnssReadBufTooSmall);

/* The NAV payload is laid out to need no compiler padding; catch any change that breaks this */
CompileTimeAssert((ATT_APP_MISSION_MAX_SATS % 4) == 0, ATT_APP_MaxSatsNotMultipleOf4);
CompileTimeAssert(sizeof(ATT_APP_NavTlm_Payload_t)
                      == (2 * sizeof(double) + 2 * sizeof(float) + 2 * sizeof(uint32) + 8 * sizeof(uint8)
                          + ATT_APP_MISSION_MAX_SATS * sizeof(ATT_APP_SatCno_t)),
                  ATT_APP_NavTlmPayloadHasPadding);

/************************************************************************
 * Local Functions
 ************************************************************************/

/*----------------------------------------------------------------
 * True when the input is a live device rather than a recorded log
 *-----------------------------------------------------------------*/
static bool ATT_APP_GNSS_IsDeviceMode(void)
{
    return (ATT_APP_Data.Gnss.InputMode == ATT_APP_GNSS_INPUT_MODE_DEVICE);
}

/*----------------------------------------------------------------
 * OSAL path of the input for the current mode
 *-----------------------------------------------------------------*/
static const char *ATT_APP_GNSS_InputPath(void)
{
    return ATT_APP_GNSS_IsDeviceMode() ? ATT_APP_GNSS_DEVICE_PATH : ATT_APP_PLATFORM_GNSS_LOG_FILE;
}

/*----------------------------------------------------------------
 * Forget everything read so far
 *-----------------------------------------------------------------*/
static void ATT_APP_GNSS_ResetStream(void)
{
    ATT_APP_GNSS_Data_t *Gnss = &ATT_APP_Data.Gnss;

    Gnss->BufHead        = 0;
    Gnss->BufTail        = 0;
    Gnss->DiscardingLine = false;
    Gnss->LinesThisPass  = 0;

    ATT_APP_NMEA_AssemblerInit(&Gnss->Assembler);
}

/*----------------------------------------------------------------
 * Open the configured input. Failure is reported, not fatal.
 *-----------------------------------------------------------------*/
static void ATT_APP_GNSS_Open(void)
{
    ATT_APP_GNSS_Data_t *Gnss = &ATT_APP_Data.Gnss;
    int32                OsStatus;

    OsStatus = OS_OpenCreate(&Gnss->FileId, ATT_APP_GNSS_InputPath(), OS_FILE_FLAG_NONE, OS_READ_ONLY);

    if (OsStatus == OS_SUCCESS)
    {
        Gnss->IsOpen = true;

        CFE_EVS_SendEvent(ATT_APP_GNSS_OPEN_INF_EID,
                          CFE_EVS_EventType_INFORMATION,
                          "ATT_APP: GNSS input %s opened",
                          ATT_APP_GNSS_InputPath());
    }
    else
    {
        Gnss->IsOpen = false;
        Gnss->FileId = OS_OBJECT_ID_UNDEFINED;

        CFE_EVS_SendEvent(ATT_APP_GNSS_OPEN_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "ATT_APP: cannot open GNSS input %s, RC = %ld; no NAV data until RESTART_LOG succeeds",
                          ATT_APP_GNSS_InputPath(),
                          (long)OsStatus);
    }
}

/*----------------------------------------------------------------
 * Close the input if it is open
 *-----------------------------------------------------------------*/
static void ATT_APP_GNSS_Close(void)
{
    ATT_APP_GNSS_Data_t *Gnss = &ATT_APP_Data.Gnss;

    if (Gnss->IsOpen)
    {
        OS_close(Gnss->FileId);
    }

    Gnss->IsOpen = false;
    Gnss->FileId = OS_OBJECT_ID_UNDEFINED;
}

/*----------------------------------------------------------------
 * Send the event for each health flag that changed in this epoch.
 * Flags that are merely still set (or still clear) send nothing.
 *-----------------------------------------------------------------*/
static void ATT_APP_GNSS_ReportHealthChanges(const ATT_APP_NMEA_Epoch_t *Epoch, const ATT_APP_HEALTH_Result_t *Result)
{
    /* Round to tenths of a dB for display; a mean above its baseline is shown as a deficit of 0 */
    float  Deficit       = (Result->MeanBaseline > Result->MeanCno) ? (Result->MeanBaseline - Result->MeanCno) : 0.0f;
    uint32 MeanTenths    = (uint32)(Result->MeanCno * ATT_APP_GNSS_TENTHS_PER_DB + 0.5f);
    uint32 DeficitTenths = (uint32)(Deficit * ATT_APP_GNSS_TENTHS_PER_DB + 0.5f);

    if (Result->Raised & ATT_APP_HEALTH_FLAG_NO_FIX)
    {
        CFE_EVS_SendEvent(ATT_APP_FIX_LOST_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "ATT_APP: GNSS fix lost at %02u:%02u:%02u UTC, %u satellites tracked",
                          (unsigned int)Epoch->Gga.UtcHour,
                          (unsigned int)Epoch->Gga.UtcMinute,
                          (unsigned int)Epoch->Gga.UtcSecond,
                          (unsigned int)Result->TrackedSats);
    }

    if (Result->Cleared & ATT_APP_HEALTH_FLAG_NO_FIX)
    {
        CFE_EVS_SendEvent(ATT_APP_FIX_REGAINED_INF_EID,
                          CFE_EVS_EventType_INFORMATION,
                          "ATT_APP: GNSS fix regained at %02u:%02u:%02u UTC, %u satellites tracked",
                          (unsigned int)Epoch->Gga.UtcHour,
                          (unsigned int)Epoch->Gga.UtcMinute,
                          (unsigned int)Epoch->Gga.UtcSecond,
                          (unsigned int)Result->TrackedSats);
    }

    if (Result->Raised & ATT_APP_HEALTH_FLAG_LOW_CNO_SAT)
    {
        CFE_EVS_SendEvent(ATT_APP_LOW_CNO_INF_EID,
                          CFE_EVS_EventType_INFORMATION,
                          "ATT_APP: low C/N0 on PRN %u: %u dB-Hz, threshold %u",
                          (unsigned int)Result->WeakestPrn,
                          (unsigned int)Result->WeakestCno,
                          (unsigned int)ATT_APP_Data.Gnss.HealthConfig.CnoThreshold);
    }

    if (Result->Cleared & ATT_APP_HEALTH_FLAG_LOW_CNO_SAT)
    {
        CFE_EVS_SendEvent(ATT_APP_LOW_CNO_CLEAR_INF_EID,
                          CFE_EVS_EventType_INFORMATION,
                          "ATT_APP: low C/N0 cleared, weakest tracked satellite now %u dB-Hz",
                          (unsigned int)Result->WeakestCno);
    }

    if (Result->Raised & ATT_APP_HEALTH_FLAG_BROADBAND_DROP)
    {
        CFE_EVS_SendEvent(ATT_APP_INTERFERENCE_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "ATT_APP: possible interference: mean C/N0 %lu.%lu dB-Hz is %lu.%lu dB below baseline, "
                          "%u of %u satellites dropped",
                          (unsigned long)(MeanTenths / ATT_APP_GNSS_TENTHS_PER_DB),
                          (unsigned long)(MeanTenths % ATT_APP_GNSS_TENTHS_PER_DB),
                          (unsigned long)(DeficitTenths / ATT_APP_GNSS_TENTHS_PER_DB),
                          (unsigned long)(DeficitTenths % ATT_APP_GNSS_TENTHS_PER_DB),
                          (unsigned int)Result->DroppedSats,
                          (unsigned int)Result->BaselinedSats);
    }

    if (Result->Cleared & ATT_APP_HEALTH_FLAG_BROADBAND_DROP)
    {
        CFE_EVS_SendEvent(ATT_APP_INTERFERENCE_CLEAR_INF_EID,
                          CFE_EVS_EventType_INFORMATION,
                          "ATT_APP: interference cleared: mean C/N0 back to %lu.%lu dB-Hz",
                          (unsigned long)(MeanTenths / ATT_APP_GNSS_TENTHS_PER_DB),
                          (unsigned long)(MeanTenths % ATT_APP_GNSS_TENTHS_PER_DB));
    }
}

/*----------------------------------------------------------------
 * Assess one finished epoch, report what changed, then fill in and
 * send its NAV packet
 *-----------------------------------------------------------------*/
static void ATT_APP_GNSS_PublishEpoch(const ATT_APP_NMEA_Epoch_t *Epoch)
{
    ATT_APP_GNSS_Data_t      *Gnss = &ATT_APP_Data.Gnss;
    ATT_APP_NavTlm_Payload_t *Nav  = &ATT_APP_Data.NavTlm.Payload;
    ATT_APP_HEALTH_Result_t   Health;
    uint8                     i;

    ++Gnss->EpochCounter;

    memset(&Health, 0, sizeof(Health));
    ATT_APP_HEALTH_Update(&Gnss->HealthState, &Gnss->HealthConfig, Epoch, &Health);

    if (Health.Raised != 0 || Health.Cleared != 0)
    {
        ATT_APP_GNSS_ReportHealthChanges(Epoch, &Health);
    }

    memset(Nav->Sats, 0, sizeof(Nav->Sats));

    for (i = 0; i < Epoch->NumSats && i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        Nav->Sats[i].Prn = Epoch->Sats[i].Prn;
        Nav->Sats[i].Cno = Epoch->Sats[i].Cno;
    }

    Nav->Latitude          = Epoch->Gga.Latitude;
    Nav->Longitude         = Epoch->Gga.Longitude;
    Nav->Altitude          = Epoch->Gga.Altitude;
    Nav->MeanCno           = Health.MeanCno;
    Nav->EpochCounter      = Gnss->EpochCounter;
    Nav->ParseErrorCounter = Gnss->ParseErrorCounter;
    Nav->UtcHour           = Epoch->Gga.UtcHour;
    Nav->UtcMinute         = Epoch->Gga.UtcMinute;
    Nav->UtcSecond         = Epoch->Gga.UtcSecond;
    Nav->UtcCentisecond    = Epoch->Gga.UtcCentisecond;
    Nav->FixQuality        = Epoch->Gga.FixQuality;
    Nav->SatsUsed          = Epoch->Gga.SatsUsed;
    Nav->SatsInView        = Epoch->SatsInView;
    Nav->HealthFlags       = Health.Flags;

    CFE_SB_TimeStampMsg(CFE_MSG_PTR(ATT_APP_Data.NavTlm.TelemetryHeader));
    CFE_SB_TransmitMsg(CFE_MSG_PTR(ATT_APP_Data.NavTlm.TelemetryHeader), true);
}

/*----------------------------------------------------------------
 * Count one rejected sentence. The event is filtered by EVS so a
 * noisy line cannot flood the event log (see ATT_APP_Init).
 *-----------------------------------------------------------------*/
static void ATT_APP_GNSS_CountParseError(ATT_APP_NMEA_Status_t Status)
{
    ++ATT_APP_Data.Gnss.ParseErrorCounter;

    CFE_EVS_SendEvent(ATT_APP_GNSS_PARSE_ERR_EID,
                      CFE_EVS_EventType_ERROR,
                      "ATT_APP: NMEA sentence rejected, reason = %d, total rejected = %lu",
                      (int)Status,
                      (unsigned long)ATT_APP_Data.Gnss.ParseErrorCounter);
}

/*----------------------------------------------------------------
 * Give one sentence to the assembler and publish the epoch it
 * completes, if any. Returns true if an epoch was published.
 *-----------------------------------------------------------------*/
static bool ATT_APP_GNSS_ProcessLine(const char *Line, size_t Length)
{
    ATT_APP_NMEA_Status_t Status;
    ATT_APP_NMEA_Epoch_t  Epoch;
    bool                  EpochReady = false;

    Status = ATT_APP_NMEA_AssemblerFeed(&ATT_APP_Data.Gnss.Assembler, Line, Length, &Epoch, &EpochReady);

    if (Status != ATT_APP_NMEA_OK)
    {
        ATT_APP_GNSS_CountParseError(Status);
    }

    if (EpochReady)
    {
        ATT_APP_GNSS_PublishEpoch(&Epoch);
    }

    return EpochReady;
}

/*----------------------------------------------------------------
 * Take the next complete line out of the buffer.
 * Returns false if no line feed has arrived yet.
 *-----------------------------------------------------------------*/
static bool ATT_APP_GNSS_NextLine(const char **Line, size_t *Length)
{
    ATT_APP_GNSS_Data_t *Gnss = &ATT_APP_Data.Gnss;
    const char          *Start;
    const char          *End;

    Start = &Gnss->Buffer[Gnss->BufHead];
    End   = memchr(Start, ATT_APP_GNSS_LINE_TERMINATOR, Gnss->BufTail - Gnss->BufHead);

    if (End == NULL)
    {
        return false;
    }

    *Line   = Start;
    *Length = (size_t)(End - Start);

    /* Consume the line and its terminator */
    Gnss->BufHead += *Length + 1;

    return true;
}

/*----------------------------------------------------------------
 * Read more bytes behind the unconsumed data.
 * Returns the OS_TimedRead result: bytes read, 0 at end of file,
 * or a negative OSAL code.
 *-----------------------------------------------------------------*/
static int32 ATT_APP_GNSS_Fill(void)
{
    ATT_APP_GNSS_Data_t *Gnss = &ATT_APP_Data.Gnss;
    int32                OsStatus;

    /* Slide the unconsumed bytes to the front to make room */
    if (Gnss->BufHead > 0)
    {
        memmove(Gnss->Buffer, &Gnss->Buffer[Gnss->BufHead], Gnss->BufTail - Gnss->BufHead);
        Gnss->BufTail -= Gnss->BufHead;
        Gnss->BufHead  = 0;
    }

    /* A full buffer with no line feed is a line longer than any sentence: drop it */
    if (Gnss->BufTail == sizeof(Gnss->Buffer))
    {
        Gnss->BufTail = 0;

        /* Count the over-long line once, however many buffers it spans */
        if (!Gnss->DiscardingLine)
        {
            Gnss->DiscardingLine = true;
            ATT_APP_GNSS_CountParseError(ATT_APP_NMEA_ERR_LENGTH);
        }
    }

    OsStatus = OS_TimedRead(Gnss->FileId,
                            &Gnss->Buffer[Gnss->BufTail],
                            sizeof(Gnss->Buffer) - Gnss->BufTail,
                            ATT_APP_PLATFORM_GNSS_READ_TIMEOUT_MS);

    if (OsStatus > 0)
    {
        Gnss->BufTail += (size_t)OsStatus;
    }

    return OsStatus;
}

/*----------------------------------------------------------------
 * End of the recorded log: publish what is pending and start over
 *-----------------------------------------------------------------*/
static void ATT_APP_GNSS_WrapLog(void)
{
    ATT_APP_GNSS_Data_t *Gnss = &ATT_APP_Data.Gnss;
    ATT_APP_NMEA_Epoch_t Epoch;

    bool HadData;

    /* A last sentence without a line feed is still a sentence */
    if (Gnss->BufTail > Gnss->BufHead && !Gnss->DiscardingLine)
    {
        ++Gnss->LinesThisPass;
        ATT_APP_GNSS_ProcessLine(&Gnss->Buffer[Gnss->BufHead], Gnss->BufTail - Gnss->BufHead);
    }

    HadData = (Gnss->LinesThisPass > 0);

    /* No further GGA will arrive to close the final epoch, so close it here */
    if (ATT_APP_NMEA_AssemblerFlush(&Gnss->Assembler, &Epoch))
    {
        ATT_APP_GNSS_PublishEpoch(&Epoch);
    }

    ATT_APP_GNSS_ResetStream();

    if (!HadData)
    {
        /* Replaying an empty file would only repeat this once per second forever */
        CFE_EVS_SendEvent(ATT_APP_GNSS_READ_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "ATT_APP: GNSS log is empty; input closed until RESTART_LOG");
        ATT_APP_GNSS_Close();
    }
    else if (OS_lseek(Gnss->FileId, 0, OS_SEEK_SET) < 0)
    {
        CFE_EVS_SendEvent(ATT_APP_GNSS_READ_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "ATT_APP: cannot rewind GNSS log; input closed until RESTART_LOG");
        ATT_APP_GNSS_Close();
    }
    else
    {
        CFE_EVS_SendEvent(ATT_APP_GNSS_WRAP_INF_EID,
                          CFE_EVS_EventType_INFORMATION,
                          "ATT_APP: end of GNSS log after %lu epochs, replaying from the start",
                          (unsigned long)Gnss->EpochCounter);
    }
}

/*----------------------------------------------------------------
 * Load the health check tuning from the platform configuration
 *-----------------------------------------------------------------*/
static void ATT_APP_GNSS_LoadHealthConfig(void)
{
    ATT_APP_HEALTH_Config_t *Config = &ATT_APP_Data.Gnss.HealthConfig;

    Config->CnoThreshold       = ATT_APP_PLATFORM_CNO_THRESHOLD_DEFAULT;
    Config->LowCnoHysteresisDb = ATT_APP_PLATFORM_LOW_CNO_HYSTERESIS_DB;
    Config->LowCnoSetCount     = ATT_APP_PLATFORM_LOW_CNO_SET_COUNT;
    Config->LowCnoClearCount   = ATT_APP_PLATFORM_LOW_CNO_CLEAR_COUNT;
    Config->NoFixSetCount      = ATT_APP_PLATFORM_NO_FIX_SET_COUNT;
    Config->NoFixClearCount    = ATT_APP_PLATFORM_NO_FIX_CLEAR_COUNT;
    Config->DropSetCount       = ATT_APP_PLATFORM_DROP_SET_COUNT;
    Config->DropClearCount     = ATT_APP_PLATFORM_DROP_CLEAR_COUNT;
    Config->DropSatPercent     = ATT_APP_PLATFORM_DROP_SAT_PERCENT;
    Config->DropMinSats        = ATT_APP_PLATFORM_DROP_MIN_SATS;
    Config->DropMeanDb         = ATT_APP_PLATFORM_DROP_MEAN_DB;
    Config->DropClearDb        = ATT_APP_PLATFORM_DROP_CLEAR_DB;
    Config->DropSatDb          = ATT_APP_PLATFORM_DROP_SAT_DB;
    Config->BaselineWindow     = ATT_APP_PLATFORM_BASELINE_WINDOW;
    Config->BaselineMinSamples = ATT_APP_PLATFORM_BASELINE_MIN_SAMPLES;
    Config->SatExpireEpochs    = ATT_APP_PLATFORM_SAT_EXPIRE_EPOCHS;
}

/************************************************************************
 * Public Functions
 ************************************************************************/

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
CFE_Status_t ATT_APP_GNSS_Init(uint8 InputMode)
{
    osal_id_t FileSysId = OS_OBJECT_ID_UNDEFINED;
    int32     OsStatus;

    memset(&ATT_APP_Data.Gnss, 0, sizeof(ATT_APP_Data.Gnss));
    ATT_APP_Data.Gnss.FileId    = OS_OBJECT_ID_UNDEFINED;
    ATT_APP_Data.Gnss.InputMode = InputMode;

    CFE_MSG_Init(CFE_MSG_PTR(ATT_APP_Data.NavTlm.TelemetryHeader),
                 CFE_SB_ValueToMsgId(ATT_APP_NAV_TLM_MID),
                 sizeof(ATT_APP_Data.NavTlm));

    ATT_APP_GNSS_ResetStream();
    ATT_APP_GNSS_LoadHealthConfig();
    ATT_APP_HEALTH_Init(&ATT_APP_Data.Gnss.HealthState);

    if (ATT_APP_GNSS_IsDeviceMode())
    {
        /* Make the host directory holding the device node visible to OSAL */
        OsStatus = OS_FileSysAddFixedMap(&FileSysId,
                                         ATT_APP_PLATFORM_GNSS_DEVICE_HOST_DIR,
                                         ATT_APP_PLATFORM_GNSS_DEVICE_MOUNT);
        if (OsStatus != OS_SUCCESS)
        {
            CFE_EVS_SendEvent(ATT_APP_GNSS_DEVMAP_ERR_EID,
                              CFE_EVS_EventType_ERROR,
                              "ATT_APP: cannot map %s to %s, RC = %ld",
                              ATT_APP_PLATFORM_GNSS_DEVICE_HOST_DIR,
                              ATT_APP_PLATFORM_GNSS_DEVICE_MOUNT,
                              (long)OsStatus);
        }
    }

    ATT_APP_GNSS_Open();

    return CFE_SUCCESS;
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void ATT_APP_GNSS_Restart(void)
{
    ATT_APP_GNSS_Close();
    ATT_APP_GNSS_ResetStream();
    ATT_APP_HEALTH_Init(&ATT_APP_Data.Gnss.HealthState);
    ATT_APP_GNSS_Open();
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void ATT_APP_GNSS_Tick(void)
{
    ATT_APP_GNSS_Data_t *Gnss      = &ATT_APP_Data.Gnss;
    const char          *Line      = NULL;
    size_t               Length    = 0;
    uint32               LineCount = 0;
    int32                OsStatus;
    bool                 Done = !Gnss->IsOpen;

    while (!Done)
    {
        if (ATT_APP_GNSS_NextLine(&Line, &Length))
        {
            ++LineCount;
            ++Gnss->LinesThisPass;

            if (Gnss->DiscardingLine)
            {
                /* This is the tail of a line that overflowed the buffer; it was already counted */
                Gnss->DiscardingLine = false;
            }
            else if (ATT_APP_GNSS_ProcessLine(Line, Length) && !ATT_APP_GNSS_IsDeviceMode())
            {
                /* Replay runs at the recorded rate: one epoch per tick */
                Done = true;
            }

            if (LineCount >= ATT_APP_PLATFORM_GNSS_MAX_LINES_PER_TICK)
            {
                Done = true;
            }
        }
        else
        {
            OsStatus = ATT_APP_GNSS_Fill();

            if (OsStatus > 0)
            {
                /* Got data: go round again and look for a complete line */
            }
            else if (OsStatus == 0 && !ATT_APP_GNSS_IsDeviceMode())
            {
                ATT_APP_GNSS_WrapLog();
                Done = true;
            }
            else if (OsStatus == 0 || OsStatus == OS_ERROR_TIMEOUT)
            {
                /* Device has nothing more to say right now */
                Done = true;
            }
            else
            {
                CFE_EVS_SendEvent(ATT_APP_GNSS_READ_ERR_EID,
                                  CFE_EVS_EventType_ERROR,
                                  "ATT_APP: GNSS read failed, RC = %ld; input closed until RESTART_LOG",
                                  (long)OsStatus);
                ATT_APP_GNSS_Close();
                Done = true;
            }
        }
    }
}
