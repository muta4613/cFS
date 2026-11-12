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
 *   This file contains the source code for the ATT_APP command dispatch (EDS build).
 *
 *   The EDS dispatcher checks message ID, length and command code against
 *   eds/att_app.xml; anything it rejects is counted as a command error here.
 */

/*
** Include Files:
*/
#include "att_app.h"
#include "att_app_dispatch.h"
#include "att_app_cmds.h"
#include "att_app_eventids.h"
#include "att_app_msgids.h"
#include "att_app_msg.h"

#include "att_app_eds_dispatcher.h"
#include "att_app_eds_dictionary.h"

/*
 * Define a lookup table for ATT_APP command codes
 */
/* clang-format off */
static const EdsDispatchTable_EdsComponent_ATT_APP_Application_CFE_SB_Telecommand_t ATT_APP_TC_DISPATCH_TABLE =
{
    .CMD =
    {
        .NoopCmd_indication            = ATT_APP_NoopCmd,
        .ResetCountersCmd_indication   = ATT_APP_ResetCountersCmd,
        .SetCnoThresholdCmd_indication = ATT_APP_SetCnoThresholdCmd,
        .RestartLogCmd_indication      = ATT_APP_RestartLogCmd
    },
    .SEND_HK =
    {
        .indication = ATT_APP_SendHkCmd
    },
    .WAKEUP =
    {
        .indication = ATT_APP_WakeupCmd
    }
};
/* clang-format on */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*     This routine will process any packet that is received on the ATT_APP   */
/*     command pipe.                                                          */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
void ATT_APP_TaskPipe(const CFE_SB_Buffer_t *SBBufPtr)
{
    CFE_Status_t      Status;
    CFE_SB_MsgId_t    MsgId;
    CFE_MSG_Size_t    MsgSize;
    CFE_MSG_FcnCode_t MsgFc;

    Status = EdsDispatch_EdsComponent_ATT_APP_Application_Telecommand(SBBufPtr, &ATT_APP_TC_DISPATCH_TABLE);

    if (Status != CFE_SUCCESS)
    {
        CFE_MSG_GetMsgId(&SBBufPtr->Msg, &MsgId);
        CFE_MSG_GetSize(&SBBufPtr->Msg, &MsgSize);
        CFE_MSG_GetFcnCode(&SBBufPtr->Msg, &MsgFc);
        ++ATT_APP_Data.CommandErrorCounter;

        if (Status == CFE_STATUS_UNKNOWN_MSG_ID)
        {
            CFE_EVS_SendEvent(ATT_APP_MID_ERR_EID,
                              CFE_EVS_EventType_ERROR,
                              "ATT_APP: invalid command packet,MID = 0x%x",
                              (unsigned int)CFE_SB_MsgIdToValue(MsgId));
        }
        else if (Status == CFE_STATUS_WRONG_MSG_LENGTH)
        {
            CFE_EVS_SendEvent(ATT_APP_CMD_LEN_ERR_EID,
                              CFE_EVS_EventType_ERROR,
                              "ATT_APP: Invalid Msg length: ID = 0x%X,  CC = %u, Len = %u",
                              (unsigned int)CFE_SB_MsgIdToValue(MsgId),
                              (unsigned int)MsgFc,
                              (unsigned int)MsgSize);
        }
        else
        {
            CFE_EVS_SendEvent(ATT_APP_CC_ERR_EID,
                              CFE_EVS_EventType_ERROR,
                              "ATT_APP: Invalid ground command code: CC = %d",
                              (int)MsgFc);
        }
    }
}
