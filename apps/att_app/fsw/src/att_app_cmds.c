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
 *   This file contains the source code for the Sample App Ground Command-handling functions
 */

/*
** Include Files:
*/
#include "att_app.h"
#include "att_app_cmds.h"
#include "att_app_msgids.h"
#include "att_app_eventids.h"
#include "att_app_version.h"
#include "att_app_tbl.h"
#include "att_app_utils.h"
#include "att_app_msg.h"

/* The sample_lib module provides the SAMPLE_Function() prototype */
#include "sample_lib.h"

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
    int i;

    /*
    ** Get command execution counters...
    */
    ATT_APP_Data.HkTlm.Payload.CommandErrorCounter = ATT_APP_Data.CommandErrorCounter;
    ATT_APP_Data.HkTlm.Payload.CommandCounter      = ATT_APP_Data.CommandCounter;
    ATT_APP_Data.HkTlm.Payload.WakeupCounter       = ATT_APP_Data.WakeupCounter;

    /*
    ** Send housekeeping telemetry packet...
    */
    CFE_SB_TimeStampMsg(CFE_MSG_PTR(ATT_APP_Data.HkTlm.TelemetryHeader));
    CFE_SB_TransmitMsg(CFE_MSG_PTR(ATT_APP_Data.HkTlm.TelemetryHeader), true);

    /*
    ** Manage any pending table loads, validations, etc.
    */
    for (i = 0; i < ATT_APP_PLATFORM_NUMBER_OF_TABLES; i++)
    {
        CFE_TBL_Manage(ATT_APP_Data.TblHandles[i]);
    }

    return CFE_SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*         Called on every scheduler wakeup (50 Hz). This is where sensor     */
/*         sampling, filtering, and fault checks will run. For now it only    */
/*         counts wakeups so the rate can be verified in telemetry.           */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
CFE_Status_t ATT_APP_WakeupCmd(const ATT_APP_WakeupCmd_t *Msg)
{
    ATT_APP_Data.WakeupCounter++;

    return CFE_SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/* SAMPLE NOOP commands                                                       */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
CFE_Status_t ATT_APP_NoopCmd(const ATT_APP_NoopCmd_t *Msg)
{
    ATT_APP_Data.CommandCounter++;

    CFE_EVS_SendEvent(ATT_APP_NOOP_INF_EID,
                      CFE_EVS_EventType_INFORMATION,
                      "SAMPLE: NOOP command from Mustafa's Build %s",
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

    CFE_EVS_SendEvent(ATT_APP_RESET_INF_EID, CFE_EVS_EventType_INFORMATION, "SAMPLE: RESET command");

    return CFE_SUCCESS;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*         This function Process Ground Station Command                       */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
CFE_Status_t ATT_APP_ProcessCmd(const ATT_APP_ProcessCmd_t *Msg)
{
    CFE_Status_t               Status;
    void                      *TblAddr;
    ATT_APP_ExampleTable_t *TblPtr;
    const char                *TableName = "ATT_APP.ExampleTable";

    /* Sample Use of Example Table */
    ATT_APP_Data.CommandCounter++;
    Status = CFE_TBL_GetAddress(&TblAddr, ATT_APP_Data.TblHandles[0]);
    if (Status < CFE_SUCCESS)
    {
        CFE_ES_WriteToSysLog("Sample App: Fail to get table address: 0x%08lx", (unsigned long)Status);
    }
    else
    {
        TblPtr = TblAddr;
        CFE_ES_WriteToSysLog("Sample App: Example Table Value 1: %d  Value 2: %d", TblPtr->Int1, TblPtr->Int2);

        ATT_APP_GetCrc(TableName);

        Status = CFE_TBL_ReleaseAddress(ATT_APP_Data.TblHandles[0]);
        if (Status != CFE_SUCCESS)
        {
            CFE_ES_WriteToSysLog("Sample App: Fail to release table address: 0x%08lx", (unsigned long)Status);
        }
        else
        {
            /* Invoke a function provided by ATT_APP_LIB */
            SAMPLE_LIB_Function();
        }
    }

    return Status;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/* A simple example command that displays a passed-in value                   */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
CFE_Status_t ATT_APP_DisplayParamCmd(const ATT_APP_DisplayParamCmd_t *Msg)
{
    ATT_APP_Data.CommandCounter++;
    CFE_EVS_SendEvent(ATT_APP_VALUE_INF_EID,
                      CFE_EVS_EventType_INFORMATION,
                      "ATT_APP: ValU32=%lu, ValI16=%d, ValStr=%s",
                      (unsigned long)Msg->Payload.ValU32,
                      (int)Msg->Payload.ValI16,
                      Msg->Payload.ValStr);

    return CFE_SUCCESS;
}
