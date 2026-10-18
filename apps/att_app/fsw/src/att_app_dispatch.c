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
 *   This file contains the source code for the Sample App.
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

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/* Verify command packet length                                               */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
bool ATT_APP_VerifyCmdLength(const CFE_MSG_Message_t *MsgPtr, size_t ExpectedLength)
{
    bool              result       = true;
    size_t            ActualLength = 0;
    CFE_SB_MsgId_t    MsgId        = CFE_SB_INVALID_MSG_ID;
    CFE_MSG_FcnCode_t FcnCode      = 0;

    CFE_MSG_GetSize(MsgPtr, &ActualLength);

    /*
    ** Verify the command packet length.
    */
    if (ExpectedLength != ActualLength)
    {
        CFE_MSG_GetMsgId(MsgPtr, &MsgId);
        CFE_MSG_GetFcnCode(MsgPtr, &FcnCode);

        CFE_EVS_SendEvent(ATT_APP_CMD_LEN_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "Invalid Msg length: ID = 0x%X,  CC = %u, Len = %u, Expected = %u",
                          (unsigned int)CFE_SB_MsgIdToValue(MsgId),
                          (unsigned int)FcnCode,
                          (unsigned int)ActualLength,
                          (unsigned int)ExpectedLength);

        result = false;

        ATT_APP_Data.CommandErrorCounter++;
    }

    return result;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/* SAMPLE ground commands                                                     */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
void ATT_APP_ProcessGroundCommand(const CFE_SB_Buffer_t *SBBufPtr)
{
    CFE_MSG_FcnCode_t CommandCode = 0;

    CFE_MSG_GetFcnCode(&SBBufPtr->Msg, &CommandCode);

    /*
    ** Process SAMPLE app ground commands
    */
    switch (CommandCode)
    {
        case ATT_APP_NOOP_CC:
            if (ATT_APP_VerifyCmdLength(&SBBufPtr->Msg, sizeof(ATT_APP_NoopCmd_t)))
            {
                ATT_APP_NoopCmd((const ATT_APP_NoopCmd_t *)SBBufPtr);
            }
            break;

        case ATT_APP_RESET_COUNTERS_CC:
            if (ATT_APP_VerifyCmdLength(&SBBufPtr->Msg, sizeof(ATT_APP_ResetCountersCmd_t)))
            {
                ATT_APP_ResetCountersCmd((const ATT_APP_ResetCountersCmd_t *)SBBufPtr);
            }
            break;

        case ATT_APP_PROCESS_CC:
            if (ATT_APP_VerifyCmdLength(&SBBufPtr->Msg, sizeof(ATT_APP_ProcessCmd_t)))
            {
                ATT_APP_ProcessCmd((const ATT_APP_ProcessCmd_t *)SBBufPtr);
            }
            break;

        case ATT_APP_DISPLAY_PARAM_CC:
            if (ATT_APP_VerifyCmdLength(&SBBufPtr->Msg, sizeof(ATT_APP_DisplayParamCmd_t)))
            {
                ATT_APP_DisplayParamCmd((const ATT_APP_DisplayParamCmd_t *)SBBufPtr);
            }
            break;

        default:
            CFE_EVS_SendEvent(ATT_APP_CC_ERR_EID,
                              CFE_EVS_EventType_ERROR,
                              "Invalid ground command code: CC = %d",
                              CommandCode);
            break;
    }
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * **/
/*                                                                            */
/*  Purpose:                                                                  */
/*     This routine will process any packet that is received on the SAMPLE    */
/*     command pipe.                                                          */
/*                                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * *  * *  * * * * */
void ATT_APP_TaskPipe(const CFE_SB_Buffer_t *SBBufPtr)
{
    static CFE_SB_MsgId_t CMD_MID     = CFE_SB_MSGID_RESERVED;
    static CFE_SB_MsgId_t SEND_HK_MID = CFE_SB_MSGID_RESERVED;

    CFE_SB_MsgId_t MsgId = CFE_SB_INVALID_MSG_ID;

    /* cache the local MID Values here, this avoids repeat lookups */
    if (!CFE_SB_IsValidMsgId(CMD_MID))
    {
        CMD_MID     = CFE_SB_ValueToMsgId(ATT_APP_CMD_MID);
        SEND_HK_MID = CFE_SB_ValueToMsgId(ATT_APP_SEND_HK_MID);
    }

    CFE_MSG_GetMsgId(&SBBufPtr->Msg, &MsgId);

    /* Process all SB messages */
    if (CFE_SB_MsgId_Equal(MsgId, SEND_HK_MID))
    {
        /* Housekeeping request */
        ATT_APP_SendHkCmd((const ATT_APP_SendHkCmd_t *)SBBufPtr);
    }
    else if (CFE_SB_MsgId_Equal(MsgId, CMD_MID))
    {
        /* Ground command */
        ATT_APP_ProcessGroundCommand(SBBufPtr);
    }
    else
    {
        /* Unknown command */
        CFE_EVS_SendEvent(ATT_APP_MID_ERR_EID,
                          CFE_EVS_EventType_ERROR,
                          "SAMPLE: invalid command packet,MID = 0x%x",
                          (unsigned int)CFE_SB_MsgIdToValue(MsgId));
    }
}
