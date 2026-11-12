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
 * @file
 *
 * Auto-Generated stub implementations for functions defined in att_app_gnss header
 */

#include "att_app_gnss.h"
#include "utgenstub.h"

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_GNSS_Init()
 * ----------------------------------------------------
 */
CFE_Status_t ATT_APP_GNSS_Init(uint8 InputMode)
{
    UT_GenStub_SetupReturnBuffer(ATT_APP_GNSS_Init, CFE_Status_t);

    UT_GenStub_AddParam(ATT_APP_GNSS_Init, uint8, InputMode);

    UT_GenStub_Execute(ATT_APP_GNSS_Init, Basic, NULL);

    return UT_GenStub_GetReturnValue(ATT_APP_GNSS_Init, CFE_Status_t);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_GNSS_Restart()
 * ----------------------------------------------------
 */
void ATT_APP_GNSS_Restart(void)
{
    UT_GenStub_Execute(ATT_APP_GNSS_Restart, Basic, NULL);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_GNSS_Tick()
 * ----------------------------------------------------
 */
void ATT_APP_GNSS_Tick(void)
{
    UT_GenStub_Execute(ATT_APP_GNSS_Tick, Basic, NULL);
}
