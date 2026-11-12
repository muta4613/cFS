/************************************************************************
 * NASA Docket No. GSC-19,200-1, and identified as "cFS Draco"
 *
 * Copyright (c) 2023 United States Government as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * All Rights Reserved.
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
 *   This file contains the source code for the ATT_APP command-handling functions
 */

/*
** Include Files:
*/
#include "att_app.h"
#include "att_app_cmds.h"
#include "att_app_msgids.h"
#include "att_app_eventids.h"
#include "att_app_version.h"
#include "att_app_msg.h"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*         This function is triggered in response to a task telemetry request */
/*         from the housekeeping task. This function will gather the Apps     */
/*         telemetry, packetize it and send it to the housekeeping task via   */
/*         the software bus                                                   */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
CFE_Status_t ATT_APP_SendHkCmd(const ATT_APP_SendHkCmd_t *Msg)
{
    /*
    ** Get command execution counters...
    */
    ATT_APP_Data.HkTlm.Payload.CommandErrorCounter = ATT_APP_Data.CommandErrorCounter;
    ATT_APP_Data.HkTlm.Payload.CommandCounter      = ATT_APP_Data.CommandCounter;
    ATT_APP_Data.HkTlm.Payload.WakeupCounter       = ATT_APP_Data.WakeupCounter;
    ATT_APP_Data.HkTlm.Payload.EpochCounter        = ATT_APP_Data.Gnss.EpochCounter;
    ATT_APP_Data.HkTlm.Payload.ParseErrorCounter   = ATT_APP_Data.Gnss.ParseErrorCounter;
    ATT_APP_Data.HkTlm.Payload.CnoThreshold        = ATT_APP_Data.Gnss.HealthConfig.CnoThreshold;
    ATT_APP_Data.HkTlm.Payload.InputState =
        ATT_APP_Data.Gnss.IsOpen ? ATT_APP_INPUT_STATE_OPEN : ATT_APP_INPUT_STATE_CLOSED;

    /*
    ** Send housekeeping telemetry packet...
    */
    CFE_SB_TimeStampMsg(CFE_MSG_PTR(ATT_APP_Data.HkTlm.TelemetryHeader));
    CFE_SB_TransmitMsg(CFE_MSG_PTR(ATT_APP_Data.HkTlm.TelemetryHeader), true);

    return CFE_SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*         Called on every scheduler wakeup (50 Hz). Counts the wakeup so    */
/*         the rate can be verified in telemetry, and services the GNSS      */
/*         input on every Nth wakeup, which gives the 1 Hz epoch rate.       */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
CFE_Status_t ATT_APP_WakeupCmd(const ATT_APP_WakeupCmd_t *Msg)
{
    ATT_APP_Data.WakeupCounter++;
    ATT_APP_Data.WakeupsSinceEpoch++;

    if (ATT_APP_Data.WakeupsSinceEpoch >= ATT_APP_PLATFORM_WAKEUPS_PER_EPOCH)
    {
        ATT_APP_Data.WakeupsSinceEpoch = 0;
        ATT_APP_GNSS_Tick();
    }

    return CFE_SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/* NOOP command                                                               */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
CFE_Status_t ATT_APP_NoopCmd(const ATT_APP_NoopCmd_t *Msg)
{
    ATT_APP_Data.CommandCounter++;

    CFE_EVS_SendEvent(ATT_APP_NOOP_INF_EID,
                      CFE_EVS_EventType_INFORMATION,
                      "ATT_APP: NOOP command, version %s",
                      ATT_APP_VERSION);

    return CFE_SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*         This function resets all the global counter variables that are     */
/*         part of the task telemetry.                                        */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
CFE_Status_t ATT_APP_ResetCountersCmd(const ATT_APP_ResetCountersCmd_t *Msg)
{
    ATT_APP_Data.CommandCounter      = 0;
    ATT_APP_Data.CommandErrorCounter = 0;
    ATT_APP_Data.WakeupCounter       = 0;

    ATT_APP_Data.Gnss.EpochCounter      = 0;
    ATT_APP_Data.Gnss.ParseErrorCounter = 0;

    CFE_EVS_SendEvent(ATT_APP_RESET_INF_EID, CFE_EVS_EventType_INFORMATION, "ATT_APP: RESET_COUNTERS command");

    return CFE_SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*         Change the C/N0 level below which a single tracked satellite is   */
/*         flagged as weak. The value is range checked: a threshold outside  */
/*         what a receiver can report would either never trip or always trip.*/
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
CFE_Status_t ATT_APP_SetCnoThresholdCmd(const ATT_APP_SetCnoThresholdCmd_t *Msg)
{
    uint8 NewThreshold = Msg->Payload.CnoThreshold;
    uint8 OldThreshold = ATT_APP_Data.Gnss.HealthConfig.CnoThreshold;

    if (NewThreshold < ATT_APP_PLATFORM_CNO_THRESHOLD_MIN || NewThreshold > ATT_APP_PLATFORM_CNO_THRESHOLD_MAX)
    {
        ATT_APP_Data.CommandErrorCounter++;

        CFE_EVS_SendEvent(ATT_APP_CNO_THRESHOLD_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "ATT_APP: C/N0 threshold %u dB-Hz rejected, valid range is %u to %u; threshold stays %u",
                          (unsigned int)NewThreshold,
                          (unsigned int)ATT_APP_PLATFORM_CNO_THRESHOLD_MIN,
                          (unsigned int)ATT_APP_PLATFORM_CNO_THRESHOLD_MAX,
                          (unsigned int)OldThreshold);

        return CFE_STATUS_VALIDATION_FAILURE;
    }

    ATT_APP_Data.Gnss.HealthConfig.CnoThreshold = NewThreshold;
    ATT_APP_Data.CommandCounter++;

    CFE_EVS_SendEvent(ATT_APP_CNO_THRESHOLD_INF_EID,
                      CFE_EVS_EventType_INFORMATION,
                      "ATT_APP: C/N0 threshold changed from %u to %u dB-Hz",
                      (unsigned int)OldThreshold,
                      (unsigned int)NewThreshold);

    return CFE_SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*         Close and reopen the GNSS input. For the recorded log this        */
/*         restarts the replay from its first sentence; it is also the way   */
/*         to recover after the input could not be opened or read.           */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
CFE_Status_t ATT_APP_RestartLogCmd(const ATT_APP_RestartLogCmd_t *Msg)
{
    ATT_APP_Data.CommandCounter++;

    CFE_EVS_SendEvent(ATT_APP_RESTART_LOG_INF_EID,
                      CFE_EVS_EventType_INFORMATION,
                      "ATT_APP: RESTART_LOG command, reopening GNSS input and clearing health history");

    ATT_APP_GNSS_Restart();

    return CFE_SUCCESS;
}
