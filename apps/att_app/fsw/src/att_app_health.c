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
 *   GNSS signal-health checks (pure functions).
 */

#include "att_app_health.h"

#include <string.h>

/************************************************************************
 * Local Macro Definitions
 ************************************************************************/

#define ATT_APP_HEALTH_PRN_UNUSED   0
#define ATT_APP_HEALTH_PERCENT      100
#define ATT_APP_HEALTH_MAX_UINT8    255
#define ATT_APP_HEALTH_MAX_UINT16   65535
#define ATT_APP_HEALTH_NO_ENTRY     (-1)
#define ATT_APP_HEALTH_MIN_DEBOUNCE 1

/************************************************************************
 * Local Functions
 ************************************************************************/

/*----------------------------------------------------------------
 * Fold one sample into a running average.
 *
 * Until the window is full this is a plain average of the samples so
 * far, so the baseline is usable quickly; after that it is an
 * exponential average with a time constant of Window samples.
 *-----------------------------------------------------------------*/
static void ATT_APP_HEALTH_Average(float *Baseline, uint16 *Samples, float Sample, uint16 Window)
{
    uint16 Divisor;

    if (*Samples < ATT_APP_HEALTH_MAX_UINT16)
    {
        ++(*Samples);
    }

    Divisor = (*Samples < Window) ? *Samples : Window;

    if (Divisor <= 1)
    {
        *Baseline = Sample;
    }
    else
    {
        *Baseline += (Sample - *Baseline) / (float)Divisor;
    }
}

/*----------------------------------------------------------------
 * Index of the baseline entry for a satellite, or NO_ENTRY
 *-----------------------------------------------------------------*/
static int32 ATT_APP_HEALTH_FindSat(const ATT_APP_HEALTH_State_t *State, uint8 Prn)
{
    int32 i;

    for (i = 0; i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        if (State->Sats[i].Prn == Prn)
        {
            return i;
        }
    }

    return ATT_APP_HEALTH_NO_ENTRY;
}

/*----------------------------------------------------------------
 * C/N0 of a satellite in this epoch, or NOT_TRACKED if it is absent
 *-----------------------------------------------------------------*/
static uint8 ATT_APP_HEALTH_EpochCno(const ATT_APP_NMEA_Epoch_t *Epoch, uint8 Prn)
{
    uint8 i;

    for (i = 0; i < Epoch->NumSats && i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        if (Epoch->Sats[i].Prn == Prn)
        {
            return Epoch->Sats[i].Cno;
        }
    }

    return ATT_APP_NMEA_CNO_NOT_TRACKED;
}

/*----------------------------------------------------------------
 * Count the satellites with a trusted baseline, and how many of
 * them are well below it or not tracked any more.
 *-----------------------------------------------------------------*/
static void ATT_APP_HEALTH_CountDropped(const ATT_APP_HEALTH_State_t  *State,
                                        const ATT_APP_HEALTH_Config_t *Config,
                                        const ATT_APP_NMEA_Epoch_t    *Epoch,
                                        ATT_APP_HEALTH_Result_t       *Result)
{
    const ATT_APP_HEALTH_SatBaseline_t *Sat;
    uint8                               Cno;
    uint8                               i;

    Result->BaselinedSats = 0;
    Result->DroppedSats   = 0;

    for (i = 0; i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        Sat = &State->Sats[i];

        if (Sat->Prn != ATT_APP_HEALTH_PRN_UNUSED && Sat->Samples >= Config->BaselineMinSamples)
        {
            ++Result->BaselinedSats;

            Cno = ATT_APP_HEALTH_EpochCno(Epoch, Sat->Prn);

            if (Cno == ATT_APP_NMEA_CNO_NOT_TRACKED || (Sat->Baseline - (float)Cno) >= Config->DropSatDb)
            {
                ++Result->DroppedSats;
            }
        }
    }
}

/*----------------------------------------------------------------
 * Pick the baseline entry a newly tracked satellite should use:
 * a free one, else the one whose satellite has been gone longest.
 * Returns NO_ENTRY if every entry belongs to a satellite tracked
 * in the previous epoch.
 *-----------------------------------------------------------------*/
static int32 ATT_APP_HEALTH_AllocSat(const ATT_APP_HEALTH_State_t *State)
{
    int32 Best       = ATT_APP_HEALTH_NO_ENTRY;
    uint8 BestMissed = 0;
    int32 i;

    for (i = 0; i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        if (State->Sats[i].Prn == ATT_APP_HEALTH_PRN_UNUSED)
        {
            return i;
        }

        if (State->Sats[i].MissedEpochs > BestMissed)
        {
            Best       = i;
            BestMissed = State->Sats[i].MissedEpochs;
        }
    }

    return Best;
}

/*----------------------------------------------------------------
 * Fold this epoch into the running averages
 *-----------------------------------------------------------------*/
static void ATT_APP_HEALTH_UpdateBaselines(ATT_APP_HEALTH_State_t        *State,
                                           const ATT_APP_HEALTH_Config_t *Config,
                                           const ATT_APP_NMEA_Epoch_t    *Epoch,
                                           const ATT_APP_HEALTH_Result_t *Result)
{
    ATT_APP_HEALTH_SatBaseline_t *Sat;
    int32                         Index;
    uint8                         i;

    if (Result->TrackedSats > 0)
    {
        ATT_APP_HEALTH_Average(&State->MeanBaseline, &State->MeanSamples, Result->MeanCno, Config->BaselineWindow);
    }

    /* Age every satellite; the ones tracked in this epoch are reset below */
    for (i = 0; i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        Sat = &State->Sats[i];

        if (Sat->Prn != ATT_APP_HEALTH_PRN_UNUSED)
        {
            if (Sat->MissedEpochs < ATT_APP_HEALTH_MAX_UINT8)
            {
                ++Sat->MissedEpochs;
            }

            /* Gone for good (set below the horizon): free the entry */
            if (Sat->MissedEpochs > Config->SatExpireEpochs
                && ATT_APP_HEALTH_EpochCno(Epoch, Sat->Prn) == ATT_APP_NMEA_CNO_NOT_TRACKED)
            {
                memset(Sat, 0, sizeof(*Sat));
            }
        }
    }

    for (i = 0; i < Epoch->NumSats && i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        if (Epoch->Sats[i].Cno != ATT_APP_NMEA_CNO_NOT_TRACKED && Epoch->Sats[i].Prn != ATT_APP_HEALTH_PRN_UNUSED)
        {
            Index = ATT_APP_HEALTH_FindSat(State, Epoch->Sats[i].Prn);

            if (Index == ATT_APP_HEALTH_NO_ENTRY)
            {
                Index = ATT_APP_HEALTH_AllocSat(State);

                if (Index != ATT_APP_HEALTH_NO_ENTRY)
                {
                    memset(&State->Sats[Index], 0, sizeof(State->Sats[Index]));
                    State->Sats[Index].Prn = Epoch->Sats[i].Prn;
                }
            }

            if (Index != ATT_APP_HEALTH_NO_ENTRY)
            {
                Sat = &State->Sats[Index];

                ATT_APP_HEALTH_Average(&Sat->Baseline,
                                       &Sat->Samples,
                                       (float)Epoch->Sats[i].Cno,
                                       Config->BaselineWindow);
                Sat->MissedEpochs = 0;
            }
        }
    }
}

/*----------------------------------------------------------------
 * Run one flag through its debounce and record the result
 *-----------------------------------------------------------------*/
static void ATT_APP_HEALTH_ApplyFlag(ATT_APP_HEALTH_Debounce_t *Debounce,
                                     bool                       Condition,
                                     uint8                      SetCount,
                                     uint8                      ClearCount,
                                     uint8                      FlagBit,
                                     ATT_APP_HEALTH_Result_t   *Result)
{
    bool WasActive = Debounce->Active;
    bool IsActive  = ATT_APP_HEALTH_Debounce(Debounce, Condition, SetCount, ClearCount);

    if (IsActive)
    {
        Result->Flags |= FlagBit;
    }

    if (IsActive && !WasActive)
    {
        Result->Raised |= FlagBit;
    }
    else if (!IsActive && WasActive)
    {
        Result->Cleared |= FlagBit;
    }
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
void ATT_APP_HEALTH_Init(ATT_APP_HEALTH_State_t *State)
{
    if (State != NULL)
    {
        memset(State, 0, sizeof(*State));
    }
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
bool ATT_APP_HEALTH_Debounce(ATT_APP_HEALTH_Debounce_t *Debounce, bool Condition, uint8 SetCount, uint8 ClearCount)
{
    uint8 Needed;

    if (Debounce == NULL)
    {
        return false;
    }

    /* While off, only "true" samples count toward a change; while on, only "false" ones */
    if (Condition != Debounce->Active)
    {
        Needed = Debounce->Active ? ClearCount : SetCount;

        if (Needed < ATT_APP_HEALTH_MIN_DEBOUNCE)
        {
            Needed = ATT_APP_HEALTH_MIN_DEBOUNCE;
        }

        ++Debounce->Count;

        if (Debounce->Count >= Needed)
        {
            Debounce->Active = Condition;
            Debounce->Count  = 0;
        }
    }
    else
    {
        /* The run was broken: start counting again */
        Debounce->Count = 0;
    }

    return Debounce->Active;
}

/*----------------------------------------------------------------
 *
 * Purpose: Implemented per public API
 *          See description in header file for argument/return detail
 *
 *-----------------------------------------------------------------*/
void ATT_APP_HEALTH_Update(ATT_APP_HEALTH_State_t        *State,
                           const ATT_APP_HEALTH_Config_t *Config,
                           const ATT_APP_NMEA_Epoch_t    *Epoch,
                           ATT_APP_HEALTH_Result_t       *Result)
{
    uint32 CnoSum      = 0;
    uint32 LowCnoLimit = 0;
    float  MeanDeficit;
    bool   BaselineTrusted;
    bool   DropCondition;
    bool   DropNow;
    uint8  i;

    if (State == NULL || Config == NULL || Epoch == NULL || Result == NULL)
    {
        return;
    }

    memset(Result, 0, sizeof(*Result));

    /*
     * This epoch at a glance: how many satellites are tracked, their mean
     * C/N0, and which one is weakest.
     */
    for (i = 0; i < Epoch->NumSats && i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        if (Epoch->Sats[i].Cno != ATT_APP_NMEA_CNO_NOT_TRACKED)
        {
            CnoSum += Epoch->Sats[i].Cno;
            ++Result->TrackedSats;

            if (Result->WeakestPrn == ATT_APP_HEALTH_PRN_UNUSED || Epoch->Sats[i].Cno < Result->WeakestCno)
            {
                Result->WeakestPrn = Epoch->Sats[i].Prn;
                Result->WeakestCno = Epoch->Sats[i].Cno;
            }
        }
    }

    if (Result->TrackedSats > 0)
    {
        Result->MeanCno = (float)CnoSum / (float)Result->TrackedSats;
    }

    Result->MeanBaseline = State->MeanBaseline;

    /*
     * Lost fix: the receiver says so itself.
     */
    ATT_APP_HEALTH_ApplyFlag(&State->NoFix,
                             Epoch->Gga.FixQuality == ATT_APP_NMEA_FIX_QUALITY_NONE,
                             Config->NoFixSetCount,
                             Config->NoFixClearCount,
                             ATT_APP_HEALTH_FLAG_NO_FIX,
                             Result);

    /*
     * Single weak satellite: once flagged, the bar to clear is higher than
     * the bar to set, so a satellite hovering at the threshold cannot make
     * the flag chatter.
     */
    LowCnoLimit = Config->CnoThreshold;
    if (State->LowCno.Active)
    {
        LowCnoLimit += Config->LowCnoHysteresisDb;
    }

    ATT_APP_HEALTH_ApplyFlag(&State->LowCno,
                             Result->TrackedSats > 0 && Result->WeakestCno < LowCnoLimit,
                             Config->LowCnoSetCount,
                             Config->LowCnoClearCount,
                             ATT_APP_HEALTH_FLAG_LOW_CNO_SAT,
                             Result);

    /*
     * Broadband drop. Two things must be true together:
     *   - the mean is well below what it has been, and
     *   - most satellites individually are well below what they have been
     *     (a satellite that vanished counts as dropped).
     * The second test is what separates interference, which hits every
     * satellite, from losing sight of part of the sky, where the
     * satellites that remain are as strong as ever.
     */
    ATT_APP_HEALTH_CountDropped(State, Config, Epoch, Result);

    BaselineTrusted = (State->MeanSamples >= Config->BaselineMinSamples);
    MeanDeficit     = State->MeanBaseline - Result->MeanCno;

    DropNow = BaselineTrusted && MeanDeficit >= Config->DropMeanDb && Result->BaselinedSats >= Config->DropMinSats
              && ((uint32)Result->DroppedSats * ATT_APP_HEALTH_PERCENT)
                     >= ((uint32)Result->BaselinedSats * Config->DropSatPercent);

    /* Setting needs the full condition; staying set only needs the mean to remain low */
    DropCondition = State->Drop.Active ? (MeanDeficit > Config->DropClearDb) : DropNow;

    ATT_APP_HEALTH_ApplyFlag(&State->Drop,
                             DropCondition,
                             Config->DropSetCount,
                             Config->DropClearCount,
                             ATT_APP_HEALTH_FLAG_BROADBAND_DROP,
                             Result);

    /*
     * Learn from this epoch only if it looks normal. If the averages kept
     * following the signal down during a drop, the "baseline" would soon
     * equal the jammed level and the alarm would clear by itself.
     */
    if (!State->Drop.Active && !DropNow)
    {
        ATT_APP_HEALTH_UpdateBaselines(State, Config, Epoch, Result);
    }
}
