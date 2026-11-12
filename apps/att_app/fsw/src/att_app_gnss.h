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
 * GNSS input handling for ATT_APP: opens the receiver data source, turns
 * its bytes into epochs once per second and publishes the NAV packet.
 */

#ifndef ATT_APP_GNSS_H
#define ATT_APP_GNSS_H

#include "cfe.h"
#include "att_app_platform_cfg.h"
#include "att_app_nmea.h"
#include "att_app_health.h"

/**
 * \brief Working data of the GNSS input
 */
typedef struct
{
    osal_id_t FileId;    /**< \brief OSAL handle of the log file or device */
    bool      IsOpen;    /**< \brief FileId is valid */
    uint8     InputMode; /**< \brief ATT_APP_GNSS_INPUT_MODE_FILE or _DEVICE, from the platform configuration */

    /*
     * Bytes read but not yet consumed live in Buffer[BufHead .. BufTail).
     * A sentence is consumed once its line feed has arrived.
     */
    char   Buffer[ATT_APP_PLATFORM_GNSS_READ_BUF_SIZE];
    size_t BufHead;
    size_t BufTail;
    bool   DiscardingLine; /**< \brief Skipping the rest of a line that was longer than the buffer */
    uint32 LinesThisPass;  /**< \brief Lines seen since the input was opened or the log last wrapped */

    ATT_APP_NMEA_Assembler_t Assembler; /**< \brief Epoch being collected */

    ATT_APP_HEALTH_Config_t HealthConfig; /**< \brief Health check tuning; CnoThreshold is commandable */
    ATT_APP_HEALTH_State_t  HealthState;  /**< \brief Health check history */

    uint32 EpochCounter;      /**< \brief Epochs published */
    uint32 ParseErrorCounter; /**< \brief Sentences rejected */
} ATT_APP_GNSS_Data_t;

/**
 * \brief Set up the GNSS input and the NAV packet
 *
 * A data source that cannot be opened is reported with an event but is not
 * an initialization failure: the app keeps running and answers commands,
 * and RESTART_LOG tries the open again.
 *
 * \param[in] InputMode ATT_APP_GNSS_INPUT_MODE_FILE or ATT_APP_GNSS_INPUT_MODE_DEVICE;
 *                      the app passes #ATT_APP_PLATFORM_GNSS_INPUT_MODE
 *
 * \return Always #CFE_SUCCESS
 */
CFE_Status_t ATT_APP_GNSS_Init(uint8 InputMode);

/**
 * \brief Close the input if it is open, forget partial data and open it again
 *
 * In file mode this restarts the replay from the first sentence. The health
 * history (flags and running averages) is cleared as well, because what
 * comes next is not a continuation of what was being read; the commanded
 * C/N0 threshold is kept.
 */
void ATT_APP_GNSS_Restart(void);

/**
 * \brief Service the input; called once per epoch period (1 Hz)
 *
 * File mode: reads sentences until one epoch is complete and publishes it;
 * at end of file the replay wraps to the start.
 * Device mode: processes everything the receiver has sent since the last
 * call and publishes each completed epoch. Never blocks.
 */
void ATT_APP_GNSS_Tick(void);

#endif /* ATT_APP_GNSS_H */
