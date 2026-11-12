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
 * Auto-Generated stub implementations for functions defined in att_app_health header
 */

#include "att_app_health.h"
#include "utgenstub.h"

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_HEALTH_Debounce()
 * ----------------------------------------------------
 */
bool ATT_APP_HEALTH_Debounce(ATT_APP_HEALTH_Debounce_t *Debounce, bool Condition, uint8 SetCount, uint8 ClearCount)
{
    UT_GenStub_SetupReturnBuffer(ATT_APP_HEALTH_Debounce, bool);

    UT_GenStub_AddParam(ATT_APP_HEALTH_Debounce, ATT_APP_HEALTH_Debounce_t *, Debounce);
    UT_GenStub_AddParam(ATT_APP_HEALTH_Debounce, bool, Condition);
    UT_GenStub_AddParam(ATT_APP_HEALTH_Debounce, uint8, SetCount);
    UT_GenStub_AddParam(ATT_APP_HEALTH_Debounce, uint8, ClearCount);

    UT_GenStub_Execute(ATT_APP_HEALTH_Debounce, Basic, NULL);

    return UT_GenStub_GetReturnValue(ATT_APP_HEALTH_Debounce, bool);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_HEALTH_Init()
 * ----------------------------------------------------
 */
void ATT_APP_HEALTH_Init(ATT_APP_HEALTH_State_t *State)
{
    UT_GenStub_AddParam(ATT_APP_HEALTH_Init, ATT_APP_HEALTH_State_t *, State);

    UT_GenStub_Execute(ATT_APP_HEALTH_Init, Basic, NULL);
}

/*
 * ----------------------------------------------------
 * Generated stub function for ATT_APP_HEALTH_Update()
 * ----------------------------------------------------
 */
void ATT_APP_HEALTH_Update(ATT_APP_HEALTH_State_t        *State,
                           const ATT_APP_HEALTH_Config_t *Config,
                           const ATT_APP_NMEA_Epoch_t    *Epoch,
                           ATT_APP_HEALTH_Result_t       *Result)
{
    UT_GenStub_AddParam(ATT_APP_HEALTH_Update, ATT_APP_HEALTH_State_t *, State);
    UT_GenStub_AddParam(ATT_APP_HEALTH_Update, const ATT_APP_HEALTH_Config_t *, Config);
    UT_GenStub_AddParam(ATT_APP_HEALTH_Update, const ATT_APP_NMEA_Epoch_t *, Epoch);
    UT_GenStub_AddParam(ATT_APP_HEALTH_Update, ATT_APP_HEALTH_Result_t *, Result);

    UT_GenStub_Execute(ATT_APP_HEALTH_Update, Basic, NULL);
}
