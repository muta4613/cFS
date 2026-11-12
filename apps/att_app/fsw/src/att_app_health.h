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
 * GNSS signal-health checks for ATT_APP.
 *
 * Like the parser, this unit is pure: no cFE or OSAL calls and no global
 * state. The caller owns the state structure, passes in one epoch at a
 * time and gets back the health flags plus which flags changed, and it is
 * the caller that turns those changes into events.
 */

#ifndef ATT_APP_HEALTH_H
#define ATT_APP_HEALTH_H

#include "common_types.h"
#include "att_app_interface_cfg.h"
#include "att_app_msgdefs.h"
#include "att_app_nmea.h"

/************************************************************************
 * Type Definitions
 ************************************************************************/

/**
 * \brief Tuning of the health checks
 *
 * Filled from the platform configuration at start-up. CnoThreshold is the
 * one field that changes at run time (SET_CNO_THRESHOLD command).
 */
typedef struct
{
    /* Single weak satellite */
    uint8 CnoThreshold;       /**< \brief A tracked satellite below this (dB-Hz) is weak */
    uint8 LowCnoHysteresisDb; /**< \brief Once flagged, every satellite must reach threshold + this to clear */
    uint8 LowCnoSetCount;     /**< \brief Consecutive epochs with a weak satellite before the flag sets */
    uint8 LowCnoClearCount;   /**< \brief Consecutive epochs without one before the flag clears */

    /* Lost fix */
    uint8 NoFixSetCount;   /**< \brief Consecutive epochs without a fix before the flag sets */
    uint8 NoFixClearCount; /**< \brief Consecutive epochs with a fix before the flag clears */

    /* Broadband drop */
    uint8  DropSetCount;       /**< \brief Consecutive epochs the drop condition must hold before the flag sets */
    uint8  DropClearCount;     /**< \brief Consecutive epochs of recovery before the flag clears */
    uint8  DropSatPercent;     /**< \brief Share of baselined satellites that must have dropped together */
    uint8  DropMinSats;        /**< \brief Fewest baselined satellites for "dropped together" to mean anything */
    float  DropMeanDb;         /**< \brief Mean C/N0 must be this far below its baseline to set */
    float  DropClearDb;        /**< \brief Mean C/N0 must be back within this of its baseline to clear */
    float  DropSatDb;          /**< \brief A satellite this far below its own baseline counts as dropped */
    uint16 BaselineWindow;     /**< \brief Length of the running averages, in epochs */
    uint16 BaselineMinSamples; /**< \brief Epochs a baseline needs before it is trusted */
    uint8  SatExpireEpochs;    /**< \brief A satellite not tracked for this long loses its baseline */
} ATT_APP_HEALTH_Config_t;

/**
 * \brief Debounced on/off condition
 *
 * The flag turns on after the condition has been true for SetCount
 * consecutive calls and off after it has been false for ClearCount.
 */
typedef struct
{
    bool  Active; /**< \brief Debounced state */
    uint8 Count;  /**< \brief Consecutive calls arguing for a change of state */
} ATT_APP_HEALTH_Debounce_t;

/** \brief Running average C/N0 of one satellite */
typedef struct
{
    float  Baseline;     /**< \brief Running average, dB-Hz */
    uint16 Samples;      /**< \brief Epochs averaged so far, saturating */
    uint8  Prn;          /**< \brief Satellite ID, 0 = entry unused */
    uint8  MissedEpochs; /**< \brief Consecutive epochs this satellite was not tracked */
} ATT_APP_HEALTH_SatBaseline_t;

/** \brief Everything the checks remember between epochs */
typedef struct
{
    ATT_APP_HEALTH_Debounce_t    NoFix;
    ATT_APP_HEALTH_Debounce_t    LowCno;
    ATT_APP_HEALTH_Debounce_t    Drop;
    float                        MeanBaseline; /**< \brief Running average of the epoch mean C/N0 */
    uint16                       MeanSamples;  /**< \brief Epochs averaged into MeanBaseline, saturating */
    ATT_APP_HEALTH_SatBaseline_t Sats[ATT_APP_MISSION_MAX_SATS];
} ATT_APP_HEALTH_State_t;

/** \brief Outcome of one epoch */
typedef struct
{
    uint8 Flags;         /**< \brief ATT_APP_HEALTH_FLAG_* bits now set */
    uint8 Raised;        /**< \brief Flags that turned on in this epoch */
    uint8 Cleared;       /**< \brief Flags that turned off in this epoch */
    uint8 TrackedSats;   /**< \brief Satellites with a C/N0 in this epoch */
    float MeanCno;       /**< \brief Mean C/N0 of the tracked satellites, 0 if none */
    float MeanBaseline;  /**< \brief Baseline the mean was compared with */
    uint8 WeakestPrn;    /**< \brief Tracked satellite with the lowest C/N0, 0 if none */
    uint8 WeakestCno;    /**< \brief Its C/N0 */
    uint8 BaselinedSats; /**< \brief Satellites with a trusted baseline */
    uint8 DroppedSats;   /**< \brief Of those, how many are well below it or no longer tracked */
} ATT_APP_HEALTH_Result_t;

/************************************************************************
 * Function Prototypes
 ************************************************************************/

/**
 * \brief Forget all history: no flags set, no baselines
 */
void ATT_APP_HEALTH_Init(ATT_APP_HEALTH_State_t *State);

/**
 * \brief Advance a debounced condition by one sample
 *
 * \param[in,out] Debounce   The condition's state
 * \param[in]     Condition  Raw condition for this sample
 * \param[in]     SetCount   Consecutive true samples needed to turn on (0 is treated as 1)
 * \param[in]     ClearCount Consecutive false samples needed to turn off (0 is treated as 1)
 *
 * \return The debounced state after this sample
 */
bool ATT_APP_HEALTH_Debounce(ATT_APP_HEALTH_Debounce_t *Debounce, bool Condition, uint8 SetCount, uint8 ClearCount);

/**
 * \brief Run all health checks on one epoch
 *
 * - NO_FIX: the receiver reports fix quality 0.
 * - LOW_CNO_SAT: a tracked satellite is below the threshold.
 * - BROADBAND_DROP: the mean C/N0 is well below its running average AND
 *   most satellites are well below their own running averages (or no
 *   longer tracked) at the same time. While that condition holds the
 *   running averages are frozen, so they cannot adapt to the interference
 *   and silently cancel the alarm.
 *
 * \param[in,out] State  History, updated by this call
 * \param[in]     Config Tuning
 * \param[in]     Epoch  The epoch to assess
 * \param[out]    Result Flags, changes and the numbers behind them
 */
void ATT_APP_HEALTH_Update(ATT_APP_HEALTH_State_t        *State,
                           const ATT_APP_HEALTH_Config_t *Config,
                           const ATT_APP_NMEA_Epoch_t    *Epoch,
                           ATT_APP_HEALTH_Result_t       *Result);

#endif /* ATT_APP_HEALTH_H */
