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

/*
** File: coveragetest_att_app.c
**
** Purpose:
** Coverage Unit Test cases for the SAMPLE Application
**
** Notes:
** This implements various test cases to exercise all code
** paths through all functions defined in the SAMPLE application.
**
** It is primarily focused at providing examples of the various
** stub configurations, hook functions, and wrapper calls that
** are often needed when coercing certain code paths through
** complex functions.
*/

/*
 * Includes
 */

#include "att_app_coveragetest_common.h"
#include "att_app.h"
#include "att_app_dispatch.h"
#include "att_app_cmds.h"

/*
**********************************************************************************
**          TEST CASE FUNCTIONS
**********************************************************************************
*/

void Test_ATT_APP_ReportHousekeeping(void)
{
    /*
     * Test Case For:
     * void ATT_APP_ReportHousekeeping( const CFE_SB_CmdHdr_t *Msg )
     */
    CFE_MSG_Message_t *MsgSend;
    CFE_MSG_Message_t *MsgTimestamp;

    /* Set up to capture send message address */
    UT_SetDataBuffer(UT_KEY(CFE_SB_TransmitMsg), &MsgSend, sizeof(MsgSend), false);

    /* Set up to capture timestamp message address */
    UT_SetDataBuffer(UT_KEY(CFE_SB_TimeStampMsg), &MsgTimestamp, sizeof(MsgTimestamp), false);

    /* Values the packet must pick up from the application data */
    ATT_APP_Data.CommandCounter         = 3;
    ATT_APP_Data.CommandErrorCounter    = 4;
    ATT_APP_Data.WakeupCounter          = 500;
    ATT_APP_Data.Gnss.EpochCounter      = 10;
    ATT_APP_Data.Gnss.ParseErrorCounter = 2;
    ATT_APP_Data.Gnss.IsOpen            = true;

    ATT_APP_Data.Gnss.HealthConfig.CnoThreshold = 33;

    /* Call unit under test, NULL pointer confirms command access is through APIs */
    ATT_APP_SendHkCmd(NULL);

    UtAssert_UINT32_EQ(ATT_APP_Data.HkTlm.Payload.CommandCounter, 3);
    UtAssert_UINT32_EQ(ATT_APP_Data.HkTlm.Payload.CommandErrorCounter, 4);
    UtAssert_UINT32_EQ(ATT_APP_Data.HkTlm.Payload.WakeupCounter, 500);
    UtAssert_UINT32_EQ(ATT_APP_Data.HkTlm.Payload.EpochCounter, 10);
    UtAssert_UINT32_EQ(ATT_APP_Data.HkTlm.Payload.ParseErrorCounter, 2);
    UtAssert_UINT32_EQ(ATT_APP_Data.HkTlm.Payload.InputState, ATT_APP_INPUT_STATE_OPEN);
    UtAssert_UINT32_EQ(ATT_APP_Data.HkTlm.Payload.CnoThreshold, 33);

    /* A closed input is reported as such */
    ATT_APP_Data.Gnss.IsOpen = false;
    ATT_APP_SendHkCmd(NULL);
    UtAssert_UINT32_EQ(ATT_APP_Data.HkTlm.Payload.InputState, ATT_APP_INPUT_STATE_CLOSED);

    /* Confirm message sent*/
    UtAssert_STUB_COUNT(CFE_SB_TransmitMsg, 2);
    UtAssert_ADDRESS_EQ(MsgSend, &ATT_APP_Data.HkTlm);

    /* Confirm timestamp msg address */
    UtAssert_STUB_COUNT(CFE_SB_TimeStampMsg, 2);
    UtAssert_ADDRESS_EQ(MsgTimestamp, &ATT_APP_Data.HkTlm);
}

void Test_ATT_APP_NoopCmd(void)
{
    /*
     * Test Case For:
     * void ATT_APP_NoopCmd( const ATT_APP_Noop_t *Msg )
     */
    ATT_APP_NoopCmd_t TestMsg;
    UT_CheckEvent_t   EventTest;

    memset(&TestMsg, 0, sizeof(TestMsg));

    /* test dispatch of NOOP */
    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_NOOP_INF_EID, NULL);

    ATT_APP_Data.CommandCounter = 0;
    UtAssert_INT32_EQ(ATT_APP_NoopCmd(&TestMsg), CFE_SUCCESS);
    UtAssert_UINT32_EQ(ATT_APP_Data.CommandCounter, 1);

    /*
     * Confirm that the event was generated
     */
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
}

void Test_ATT_APP_WakeupCmd(void)
{
    /*
     * Test Case For:
     * CFE_Status_t ATT_APP_WakeupCmd( const ATT_APP_WakeupCmd_t *Msg )
     *
     * Each wakeup increments the counter by exactly one, and the GNSS
     * input is serviced once per ATT_APP_PLATFORM_WAKEUPS_PER_EPOCH wakeups.
     */
    ATT_APP_WakeupCmd_t TestMsg;
    uint32              i;

    memset(&TestMsg, 0, sizeof(TestMsg));
    memset(&ATT_APP_Data, 0, sizeof(ATT_APP_Data));

    UtAssert_INT32_EQ(ATT_APP_WakeupCmd(&TestMsg), CFE_SUCCESS);
    UtAssert_INT32_EQ(ATT_APP_WakeupCmd(&TestMsg), CFE_SUCCESS);
    UtAssert_UINT32_EQ(ATT_APP_Data.WakeupCounter, 2);

    /* One wakeup short of an epoch period: input not serviced yet */
    for (i = 2; i < ATT_APP_PLATFORM_WAKEUPS_PER_EPOCH - 1; ++i)
    {
        ATT_APP_WakeupCmd(&TestMsg);
    }
    UtAssert_STUB_COUNT(ATT_APP_GNSS_Tick, 0);

    /* The wakeup that completes the period services it once */
    ATT_APP_WakeupCmd(&TestMsg);
    UtAssert_STUB_COUNT(ATT_APP_GNSS_Tick, 1);
    UtAssert_UINT32_EQ(ATT_APP_Data.WakeupsSinceEpoch, 0);
    UtAssert_UINT32_EQ(ATT_APP_Data.WakeupCounter, ATT_APP_PLATFORM_WAKEUPS_PER_EPOCH);

    /* And again exactly one period later, not before */
    for (i = 0; i < ATT_APP_PLATFORM_WAKEUPS_PER_EPOCH; ++i)
    {
        UtAssert_STUB_COUNT(ATT_APP_GNSS_Tick, 1);
        ATT_APP_WakeupCmd(&TestMsg);
    }
    UtAssert_STUB_COUNT(ATT_APP_GNSS_Tick, 2);
}

void Test_ATT_APP_ResetCountersCmd(void)
{
    /*
     * Test Case For:
     * void ATT_APP_ResetCounters( const ATT_APP_ResetCounters_t *Msg )
     */
    ATT_APP_ResetCountersCmd_t TestMsg;
    UT_CheckEvent_t            EventTest;

    memset(&TestMsg, 0, sizeof(TestMsg));

    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_RESET_INF_EID, "ATT_APP: RESET_COUNTERS command");

    ATT_APP_Data.WakeupCounter = 7;

    UtAssert_INT32_EQ(ATT_APP_ResetCountersCmd(&TestMsg), CFE_SUCCESS);
    UtAssert_UINT32_EQ(ATT_APP_Data.WakeupCounter, 0);

    /*
     * Confirm that the event was generated
     */
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
}

void Test_ATT_APP_SetCnoThresholdCmd(void)
{
    /*
     * Test Case For:
     * CFE_Status_t ATT_APP_SetCnoThresholdCmd( const ATT_APP_SetCnoThresholdCmd_t *Msg )
     *
     * Values inside the configured range are applied and counted as
     * accepted; values outside it are counted as errors and change nothing.
     */
    ATT_APP_SetCnoThresholdCmd_t TestMsg;
    UT_CheckEvent_t              AcceptEvent;
    UT_CheckEvent_t              RejectEvent;

    memset(&TestMsg, 0, sizeof(TestMsg));
    memset(&ATT_APP_Data, 0, sizeof(ATT_APP_Data));
    ATT_APP_Data.Gnss.HealthConfig.CnoThreshold = ATT_APP_PLATFORM_CNO_THRESHOLD_DEFAULT;

    /* A typical value */
    UT_CHECKEVENT_SETUP(&AcceptEvent, ATT_APP_CNO_THRESHOLD_INF_EID, NULL);
    TestMsg.Payload.CnoThreshold = 35;
    UtAssert_INT32_EQ(ATT_APP_SetCnoThresholdCmd(&TestMsg), CFE_SUCCESS);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.HealthConfig.CnoThreshold, 35);
    UtAssert_UINT32_EQ(ATT_APP_Data.CommandCounter, 1);
    UtAssert_UINT32_EQ(ATT_APP_Data.CommandErrorCounter, 0);
    UtAssert_UINT32_EQ(AcceptEvent.MatchCount, 1);

    /* Both ends of the range are valid */
    TestMsg.Payload.CnoThreshold = ATT_APP_PLATFORM_CNO_THRESHOLD_MIN;
    UtAssert_INT32_EQ(ATT_APP_SetCnoThresholdCmd(&TestMsg), CFE_SUCCESS);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.HealthConfig.CnoThreshold, ATT_APP_PLATFORM_CNO_THRESHOLD_MIN);
    TestMsg.Payload.CnoThreshold = ATT_APP_PLATFORM_CNO_THRESHOLD_MAX;
    UtAssert_INT32_EQ(ATT_APP_SetCnoThresholdCmd(&TestMsg), CFE_SUCCESS);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.HealthConfig.CnoThreshold, ATT_APP_PLATFORM_CNO_THRESHOLD_MAX);
    UtAssert_UINT32_EQ(ATT_APP_Data.CommandCounter, 3);
    UtAssert_UINT32_EQ(AcceptEvent.MatchCount, 3);

    /* One below the minimum, one above the maximum, and the extremes of a uint8 */
    UT_CHECKEVENT_SETUP(&RejectEvent, ATT_APP_CNO_THRESHOLD_ERR_EID, NULL);
    TestMsg.Payload.CnoThreshold = ATT_APP_PLATFORM_CNO_THRESHOLD_MIN - 1;
    UtAssert_INT32_EQ(ATT_APP_SetCnoThresholdCmd(&TestMsg), CFE_STATUS_VALIDATION_FAILURE);
    TestMsg.Payload.CnoThreshold = ATT_APP_PLATFORM_CNO_THRESHOLD_MAX + 1;
    UtAssert_INT32_EQ(ATT_APP_SetCnoThresholdCmd(&TestMsg), CFE_STATUS_VALIDATION_FAILURE);
    TestMsg.Payload.CnoThreshold = 0;
    UtAssert_INT32_EQ(ATT_APP_SetCnoThresholdCmd(&TestMsg), CFE_STATUS_VALIDATION_FAILURE);
    TestMsg.Payload.CnoThreshold = 255;
    UtAssert_INT32_EQ(ATT_APP_SetCnoThresholdCmd(&TestMsg), CFE_STATUS_VALIDATION_FAILURE);

    /* Rejected commands: counted as errors, one event each, threshold untouched */
    UtAssert_UINT32_EQ(ATT_APP_Data.CommandErrorCounter, 4);
    UtAssert_UINT32_EQ(ATT_APP_Data.CommandCounter, 3);
    UtAssert_UINT32_EQ(RejectEvent.MatchCount, 4);
    UtAssert_UINT32_EQ(ATT_APP_Data.Gnss.HealthConfig.CnoThreshold, ATT_APP_PLATFORM_CNO_THRESHOLD_MAX);
}

void Test_ATT_APP_RestartLogCmd(void)
{
    /*
     * Test Case For:
     * CFE_Status_t ATT_APP_RestartLogCmd( const ATT_APP_RestartLogCmd_t *Msg )
     */
    ATT_APP_RestartLogCmd_t TestMsg;
    UT_CheckEvent_t         EventTest;

    memset(&TestMsg, 0, sizeof(TestMsg));
    memset(&ATT_APP_Data, 0, sizeof(ATT_APP_Data));

    UT_CHECKEVENT_SETUP(&EventTest, ATT_APP_RESTART_LOG_INF_EID, NULL);
    UtAssert_INT32_EQ(ATT_APP_RestartLogCmd(&TestMsg), CFE_SUCCESS);

    UtAssert_STUB_COUNT(ATT_APP_GNSS_Restart, 1);
    UtAssert_UINT32_EQ(ATT_APP_Data.CommandCounter, 1);
    UtAssert_UINT32_EQ(ATT_APP_Data.CommandErrorCounter, 0);
    UtAssert_UINT32_EQ(EventTest.MatchCount, 1);
}

/*
 * Register the test cases to execute with the unit test tool
 */
void UtTest_Setup(void)
{
    ADD_TEST(ATT_APP_ReportHousekeeping);
    ADD_TEST(ATT_APP_WakeupCmd);
    ADD_TEST(ATT_APP_NoopCmd);
    ADD_TEST(ATT_APP_ResetCountersCmd);
    ADD_TEST(ATT_APP_SetCnoThresholdCmd);
    ADD_TEST(ATT_APP_RestartLogCmd);
}
