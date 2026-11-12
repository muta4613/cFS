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

/*
** File: coveragetest_att_app_health.c
**
** Purpose:
** Coverage Unit Test cases for the ATT_APP signal-health checks.
**
** Notes:
** The health logic is pure, so each test simply plays a sequence of
** epochs through it and checks the flags after every one. The scenarios
** mirror the scripted events in the sample log.
*/

/*
 * Includes
 */
#include "att_app_coveragetest_common.h"
#include "att_app_health.h"

#define UT_HEALTH_NUM_SATS    10
#define UT_HEALTH_NOMINAL_CNO 43
#define UT_HEALTH_WARMUP      20

#define UT_NO_FIX ATT_APP_HEALTH_FLAG_NO_FIX
#define UT_LOW    ATT_APP_HEALTH_FLAG_LOW_CNO_SAT
#define UT_DROP   ATT_APP_HEALTH_FLAG_BROADBAND_DROP

static const uint8 UT_HEALTH_PRNS[UT_HEALTH_NUM_SATS] = { 2, 5, 7, 9, 13, 15, 18, 21, 26, 30 };

static ATT_APP_HEALTH_State_t  UT_State;
static ATT_APP_HEALTH_Config_t UT_Config;
static ATT_APP_HEALTH_Result_t UT_Result;
static ATT_APP_NMEA_Epoch_t    UT_Epoch;

/* The flight defaults, so the tests exercise the tuning that actually flies */
static void UT_HEALTH_Setup(void)
{
    memset(&UT_Config, 0, sizeof(UT_Config));

    UT_Config.CnoThreshold       = ATT_APP_PLATFORM_CNO_THRESHOLD_DEFAULT;
    UT_Config.LowCnoHysteresisDb = ATT_APP_PLATFORM_LOW_CNO_HYSTERESIS_DB;
    UT_Config.LowCnoSetCount     = ATT_APP_PLATFORM_LOW_CNO_SET_COUNT;
    UT_Config.LowCnoClearCount   = ATT_APP_PLATFORM_LOW_CNO_CLEAR_COUNT;
    UT_Config.NoFixSetCount      = ATT_APP_PLATFORM_NO_FIX_SET_COUNT;
    UT_Config.NoFixClearCount    = ATT_APP_PLATFORM_NO_FIX_CLEAR_COUNT;
    UT_Config.DropSetCount       = ATT_APP_PLATFORM_DROP_SET_COUNT;
    UT_Config.DropClearCount     = ATT_APP_PLATFORM_DROP_CLEAR_COUNT;
    UT_Config.DropSatPercent     = ATT_APP_PLATFORM_DROP_SAT_PERCENT;
    UT_Config.DropMinSats        = ATT_APP_PLATFORM_DROP_MIN_SATS;
    UT_Config.DropMeanDb         = ATT_APP_PLATFORM_DROP_MEAN_DB;
    UT_Config.DropClearDb        = ATT_APP_PLATFORM_DROP_CLEAR_DB;
    UT_Config.DropSatDb          = ATT_APP_PLATFORM_DROP_SAT_DB;
    UT_Config.BaselineWindow     = ATT_APP_PLATFORM_BASELINE_WINDOW;
    UT_Config.BaselineMinSamples = ATT_APP_PLATFORM_BASELINE_MIN_SAMPLES;
    UT_Config.SatExpireEpochs    = ATT_APP_PLATFORM_SAT_EXPIRE_EPOCHS;

    ATT_APP_HEALTH_Init(&UT_State);
    memset(&UT_Result, 0, sizeof(UT_Result));
}

/* Ten satellites, all at the same C/N0, with a fix */
static void UT_HEALTH_NominalEpoch(uint8 Cno)
{
    uint8 i;

    memset(&UT_Epoch, 0, sizeof(UT_Epoch));
    UT_Epoch.Gga.FixQuality = 1;
    UT_Epoch.Gga.SatsUsed   = UT_HEALTH_NUM_SATS;
    UT_Epoch.SatsInView     = UT_HEALTH_NUM_SATS;
    UT_Epoch.NumSats        = UT_HEALTH_NUM_SATS;

    for (i = 0; i < UT_HEALTH_NUM_SATS; ++i)
    {
        UT_Epoch.Sats[i].Prn = UT_HEALTH_PRNS[i];
        UT_Epoch.Sats[i].Cno = Cno;
    }
}

/* Run the epoch currently in UT_Epoch and return the flags */
static uint8 UT_HEALTH_Step(void)
{
    ATT_APP_HEALTH_Update(&UT_State, &UT_Config, &UT_Epoch, &UT_Result);
    return UT_Result.Flags;
}

/* Run Count identical nominal epochs; no flag may be set at any point */
static void UT_HEALTH_RunNominal(uint32 Count)
{
    uint32 i;
    uint8  Seen = 0;

    for (i = 0; i < Count; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
        Seen |= UT_HEALTH_Step();
    }

    UtAssert_UINT32_EQ(Seen, 0);
}

/*
**********************************************************************************
**          TEST CASE FUNCTIONS
**********************************************************************************
*/

void Test_ATT_APP_HEALTH_Debounce(void)
{
    /*
     * Test Case For:
     * bool ATT_APP_HEALTH_Debounce(ATT_APP_HEALTH_Debounce_t *, bool, uint8, uint8)
     */
    ATT_APP_HEALTH_Debounce_t Debounce;

    memset(&Debounce, 0, sizeof(Debounce));

    UtAssert_True(!ATT_APP_HEALTH_Debounce(NULL, true, 1, 1), "NULL is tolerated");

    /* Set needs 3 in a row: a broken run starts over */
    UtAssert_True(!ATT_APP_HEALTH_Debounce(&Debounce, true, 3, 2), "1 of 3");
    UtAssert_True(!ATT_APP_HEALTH_Debounce(&Debounce, true, 3, 2), "2 of 3");
    UtAssert_True(!ATT_APP_HEALTH_Debounce(&Debounce, false, 3, 2), "run broken");
    UtAssert_True(!ATT_APP_HEALTH_Debounce(&Debounce, true, 3, 2), "1 of 3 again");
    UtAssert_True(!ATT_APP_HEALTH_Debounce(&Debounce, true, 3, 2), "2 of 3 again");
    UtAssert_True(ATT_APP_HEALTH_Debounce(&Debounce, true, 3, 2), "3 of 3: on");

    /* Clear needs 2 in a row, and a broken run starts over too */
    UtAssert_True(ATT_APP_HEALTH_Debounce(&Debounce, false, 3, 2), "1 of 2 to clear");
    UtAssert_True(ATT_APP_HEALTH_Debounce(&Debounce, true, 3, 2), "clear run broken");
    UtAssert_True(ATT_APP_HEALTH_Debounce(&Debounce, false, 3, 2), "1 of 2 again");
    UtAssert_True(!ATT_APP_HEALTH_Debounce(&Debounce, false, 3, 2), "2 of 2: off");
    UtAssert_True(!ATT_APP_HEALTH_Debounce(&Debounce, false, 3, 2), "stays off");

    /* A count of 0 would mean "never"; it is treated as 1 instead */
    UtAssert_True(ATT_APP_HEALTH_Debounce(&Debounce, true, 0, 0), "set count 0 acts as 1");
    UtAssert_True(!ATT_APP_HEALTH_Debounce(&Debounce, false, 0, 0), "clear count 0 acts as 1");
}

void Test_ATT_APP_HEALTH_NullAndInit(void)
{
    /*
     * Test Case For:
     * void ATT_APP_HEALTH_Init(), void ATT_APP_HEALTH_Update() argument checks
     */
    ATT_APP_HEALTH_Result_t Untouched;

    UT_HEALTH_Setup();
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);

    ATT_APP_HEALTH_Init(NULL); /* must not crash */

    memset(&UT_Result, 0x5A, sizeof(UT_Result));
    memset(&Untouched, 0x5A, sizeof(Untouched));

    ATT_APP_HEALTH_Update(NULL, &UT_Config, &UT_Epoch, &UT_Result);
    ATT_APP_HEALTH_Update(&UT_State, NULL, &UT_Epoch, &UT_Result);
    ATT_APP_HEALTH_Update(&UT_State, &UT_Config, NULL, &UT_Result);
    ATT_APP_HEALTH_Update(&UT_State, &UT_Config, &UT_Epoch, NULL);
    UtAssert_MemCmp(&UT_Result, &Untouched, sizeof(UT_Result), "Result untouched by rejected calls");
    UtAssert_UINT32_EQ(UT_State.MeanSamples, 0);

    /* Init wipes history */
    UT_HEALTH_Step();
    UtAssert_UINT32_EQ(UT_State.MeanSamples, 1);
    ATT_APP_HEALTH_Init(&UT_State);
    UtAssert_UINT32_EQ(UT_State.MeanSamples, 0);
    UtAssert_UINT32_EQ(UT_State.Sats[0].Prn, 0);
}

void Test_ATT_APP_HEALTH_Nominal(void)
{
    /*
     * Test Case For:
     * void ATT_APP_HEALTH_Update()
     *
     * Steady signals: no flags, and the summary numbers are right.
     */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);

    /* One satellite in view but not tracked is left out of the mean */
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UT_Epoch.Sats[0].Cno = 37;
    UT_Epoch.Sats[9].Cno = ATT_APP_NMEA_CNO_NOT_TRACKED;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);
    UtAssert_UINT32_EQ(UT_Result.TrackedSats, 9);
    UtAssert_UINT32_EQ(UT_Result.WeakestPrn, 2);
    UtAssert_UINT32_EQ(UT_Result.WeakestCno, 37);
    UtAssert_True(UT_Result.MeanCno == (float)(8 * UT_HEALTH_NOMINAL_CNO + 37) / 9.0f,
                  "MeanCno %.3f",
                  (double)UT_Result.MeanCno);
    UtAssert_True(UT_Result.MeanBaseline == (float)UT_HEALTH_NOMINAL_CNO,
                  "MeanBaseline %.3f",
                  (double)UT_Result.MeanBaseline);
    UtAssert_UINT32_EQ(UT_Result.BaselinedSats, UT_HEALTH_NUM_SATS);
    UtAssert_UINT32_EQ(UT_Result.DroppedSats, 2); /* the 37 (6 dB down) and the untracked one */
    UtAssert_UINT32_EQ(UT_Result.Raised, 0);
    UtAssert_UINT32_EQ(UT_Result.Cleared, 0);

    /* Nothing tracked at all: mean is 0, no weakest satellite, no divide by zero */
    UT_HEALTH_Setup();
    memset(&UT_Epoch, 0, sizeof(UT_Epoch));
    UT_Epoch.Gga.FixQuality = 1;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);
    UtAssert_UINT32_EQ(UT_Result.TrackedSats, 0);
    UtAssert_UINT32_EQ(UT_Result.WeakestPrn, 0);
    UtAssert_True(UT_Result.MeanCno == 0.0f, "MeanCno 0 with nothing tracked");
    UtAssert_UINT32_EQ(UT_State.MeanSamples, 0);

    /* An epoch claiming more satellites than the array holds is clamped, not overrun */
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UT_Epoch.NumSats = 255;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);
    UtAssert_UINT32_EQ(UT_Result.TrackedSats, UT_HEALTH_NUM_SATS);
}

void Test_ATT_APP_HEALTH_NoFix(void)
{
    /*
     * Test Case For:
     * void ATT_APP_HEALTH_Update() - NO_FIX
     *
     * Sets on the first epoch without a fix, clears only after the fix has
     * been back for NoFixClearCount epochs in a row.
     */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);

    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UT_Epoch.Gga.FixQuality = ATT_APP_NMEA_FIX_QUALITY_NONE;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_NO_FIX);
    UtAssert_UINT32_EQ(UT_Result.Raised, UT_NO_FIX);
    UtAssert_UINT32_EQ(UT_Result.Cleared, 0);

    /* Still no fix: flag stays, but it is not "raised" again */
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_NO_FIX);
    UtAssert_UINT32_EQ(UT_Result.Raised, 0);

    /* One good epoch is not enough; a relapse restarts the count */
    UT_Epoch.Gga.FixQuality = 1;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_NO_FIX);
    UT_Epoch.Gga.FixQuality = ATT_APP_NMEA_FIX_QUALITY_NONE;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_NO_FIX);
    UT_Epoch.Gga.FixQuality = 1;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_NO_FIX);
    UtAssert_UINT32_EQ(UT_Result.Cleared, 0);

    /* Second good epoch in a row clears it, once */
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);
    UtAssert_UINT32_EQ(UT_Result.Cleared, UT_NO_FIX);
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);
    UtAssert_UINT32_EQ(UT_Result.Cleared, 0);
}

void Test_ATT_APP_HEALTH_LowCno(void)
{
    /*
     * Test Case For:
     * void ATT_APP_HEALTH_Update() - LOW_CNO_SAT
     *
     * One satellite fades on its own (PRN 21 in the sample log). The flag
     * sets below the threshold and clears only above threshold + hysteresis.
     */
    const uint8 Threshold = ATT_APP_PLATFORM_CNO_THRESHOLD_DEFAULT;
    const uint8 ClearAt   = ATT_APP_PLATFORM_CNO_THRESHOLD_DEFAULT + ATT_APP_PLATFORM_LOW_CNO_HYSTERESIS_DB;

    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);

    /* Exactly at the threshold is not below it */
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UT_Epoch.Sats[7].Cno = Threshold;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);

    /* One below: set, and the result names the satellite */
    UT_Epoch.Sats[7].Cno = Threshold - 1;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_LOW);
    UtAssert_UINT32_EQ(UT_Result.Raised, UT_LOW);
    UtAssert_UINT32_EQ(UT_Result.WeakestPrn, 21);
    UtAssert_UINT32_EQ(UT_Result.WeakestCno, Threshold - 1);

    /* Deep fade for a while: stays set, no repeat "raised", and never looks like a broadband drop */
    UT_Epoch.Sats[7].Cno = Threshold - 4;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_LOW);
    UtAssert_UINT32_EQ(UT_Result.Raised, 0);
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_LOW);

    /* Back at the threshold, and one above it: inside the hysteresis band, still set */
    UT_Epoch.Sats[7].Cno = Threshold;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_LOW);
    UT_Epoch.Sats[7].Cno = ClearAt - 1;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_LOW);
    UtAssert_UINT32_EQ(UT_Result.Cleared, 0);

    /* Reaches threshold + hysteresis: clears, once */
    UT_Epoch.Sats[7].Cno = ClearAt;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);
    UtAssert_UINT32_EQ(UT_Result.Cleared, UT_LOW);

    /* Inside the band from above does not set it again */
    UT_Epoch.Sats[7].Cno = Threshold;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);

    /* A satellite that is in view but not tracked is not a weak satellite */
    UT_Epoch.Sats[7].Cno = ATT_APP_NMEA_CNO_NOT_TRACKED;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);

    /* The threshold is live: raising it above the nominal level flags at once */
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UT_Config.CnoThreshold = UT_HEALTH_NOMINAL_CNO + 1;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), UT_LOW);
    UT_Config.CnoThreshold = Threshold;
    UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);
}

void Test_ATT_APP_HEALTH_BroadbandDrop(void)
{
    /*
     * Test Case For:
     * void ATT_APP_HEALTH_Update() - BROADBAND_DROP
     *
     * Every satellite falls 12 dB at once and stays there for longer than
     * the averaging window, then recovers.
     */
    const uint8 Jammed = UT_HEALTH_NOMINAL_CNO - 12;
    float       BaselineBefore;
    uint32      i;

    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);
    BaselineBefore = UT_State.MeanBaseline;

    /* First epoch of the drop: condition seen but not yet confirmed */
    UT_HEALTH_NominalEpoch(Jammed);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
    UtAssert_UINT32_EQ(UT_Result.DroppedSats, UT_HEALTH_NUM_SATS);
    UtAssert_UINT32_EQ(UT_Result.BaselinedSats, UT_HEALTH_NUM_SATS);

    /* Second consecutive epoch: set, raised exactly once */
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);
    UtAssert_UINT32_EQ(UT_Result.Raised & UT_DROP, UT_DROP);
    UtAssert_True(UT_Result.MeanCno == (float)Jammed, "MeanCno %.2f", (double)UT_Result.MeanCno);
    UtAssert_True(UT_Result.MeanBaseline == BaselineBefore, "Baseline reported is the pre-drop one");

    /*
     * Hold the drop for three averaging windows. The flag must stay set
     * and the baselines must not move: if they adapted, the alarm would
     * cancel itself while the interference is still there.
     */
    for (i = 0; i < 3 * ATT_APP_PLATFORM_BASELINE_WINDOW; ++i)
    {
        UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);
        UtAssert_UINT32_EQ(UT_Result.Raised & UT_DROP, 0);
    }
    UtAssert_True(UT_State.MeanBaseline == BaselineBefore,
                  "Mean baseline frozen at %.2f",
                  (double)UT_State.MeanBaseline);
    UtAssert_True(UT_State.Sats[0].Baseline == (float)UT_HEALTH_NOMINAL_CNO,
                  "Satellite baseline frozen at %.2f",
                  (double)UT_State.Sats[0].Baseline);

    /* Partial recovery, still more than the clear margin below baseline: stays set */
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO - 5);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);

    /* Recovery interrupted after two good epochs: the clear count starts over */
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);
    UT_HEALTH_NominalEpoch(Jammed);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);

    /* Three good epochs in a row: cleared, once */
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
    UtAssert_UINT32_EQ(UT_Result.Cleared & UT_DROP, UT_DROP);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
    UtAssert_UINT32_EQ(UT_Result.Cleared, 0);

    /* A single-epoch glitch is ignored, and does not drag the baseline down */
    UT_HEALTH_NominalEpoch(Jammed);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
    UtAssert_True(UT_State.MeanBaseline == BaselineBefore, "Glitch not averaged in");

    /* Total loss of every satellite also counts (mean falls to 0, all dropped) */
    memset(UT_Epoch.Sats, 0, sizeof(UT_Epoch.Sats));
    UT_Epoch.NumSats = 0;
    UT_HEALTH_Step();
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, UT_DROP);
    UtAssert_UINT32_EQ(UT_Result.DroppedSats, UT_HEALTH_NUM_SATS);
}

void Test_ATT_APP_HEALTH_DropNotTripped(void)
{
    /*
     * Test Case For:
     * void ATT_APP_HEALTH_Update() - things that must NOT raise BROADBAND_DROP
     */
    uint32 i;
    uint8  Seen;

    /*
     * The no-fix window of the sample log: 7 of 10 satellites stop being
     * tracked, the 3 that remain are as strong as before. Many satellites
     * "dropped", but the mean did not, so this is lost sky, not interference.
     */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);
    Seen = 0;
    for (i = 0; i < 10; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
        UT_Epoch.Gga.FixQuality = ATT_APP_NMEA_FIX_QUALITY_NONE;
        UT_Epoch.Sats[1].Cno = UT_Epoch.Sats[3].Cno = UT_Epoch.Sats[4].Cno = UT_Epoch.Sats[5].Cno = 0;
        UT_Epoch.Sats[7].Cno = UT_Epoch.Sats[8].Cno = UT_Epoch.Sats[9].Cno  = 0;
        Seen                                                               |= UT_HEALTH_Step();
        UtAssert_UINT32_EQ(UT_Result.TrackedSats, 3);
        UtAssert_UINT32_EQ(UT_Result.DroppedSats, 7);
    }
    UtAssert_UINT32_EQ(Seen, UT_NO_FIX);

    /* Even if 8 of 10 vanish (over the 75% share), an unchanged mean keeps the flag clear */
    for (i = 0; i < 5; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
        memset(&UT_Epoch.Sats[2], 0, 8 * sizeof(UT_Epoch.Sats[0]));
        UT_Epoch.NumSats = 2;
        UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
        UtAssert_UINT32_EQ(UT_Result.DroppedSats, 8);
    }

    /*
     * The opposite case: the mean is well down because half the satellites
     * collapsed, but the other half are untouched. Mean test passes, share
     * test fails: not a broadband event.
     */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);
    for (i = 0; i < 5; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
        UT_Epoch.Sats[0].Cno = UT_Epoch.Sats[1].Cno = UT_Epoch.Sats[2].Cno = 23;
        UT_Epoch.Sats[3].Cno = UT_Epoch.Sats[4].Cno = 23;
        UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
        UtAssert_UINT32_EQ(UT_Result.DroppedSats, 5);
    }

    /* A fall just short of the mean threshold, on every satellite */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);
    for (i = 0; i < 5; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO - 7);
        UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
    }

    /*
     * A receiver that is switched on while the signal is already low has
     * nothing to compare with: the low level is learned as its normal.
     * (Known limitation: the check detects a change, not an absolute level.)
     */
    UT_HEALTH_Setup();
    for (i = 0; i < 2 * UT_HEALTH_WARMUP; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO - 12);
        UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
    }

    /* Before the baseline is trusted, a fall is not judged at all */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(ATT_APP_PLATFORM_BASELINE_MIN_SAMPLES - 1);
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO - 15);
    UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
    UtAssert_UINT32_EQ(UT_Result.BaselinedSats, 0);

    /* Too few satellites for "most of them dropped" to mean anything */
    UT_HEALTH_Setup();
    for (i = 0; i < UT_HEALTH_WARMUP; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
        UT_Epoch.NumSats = ATT_APP_PLATFORM_DROP_MIN_SATS - 1;
        UT_HEALTH_Step();
    }
    for (i = 0; i < 5; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO - 15);
        UT_Epoch.NumSats = ATT_APP_PLATFORM_DROP_MIN_SATS - 1;
        UtAssert_UINT32_EQ(UT_HEALTH_Step() & UT_DROP, 0);
        UtAssert_UINT32_EQ(UT_Result.BaselinedSats, ATT_APP_PLATFORM_DROP_MIN_SATS - 1);
    }
}

void Test_ATT_APP_HEALTH_Baselines(void)
{
    /*
     * Test Case For:
     * void ATT_APP_HEALTH_Update() - running averages and the satellite table
     */
    uint32 i;
    int32  Found;

    /* Slow change is followed: 3 dB lower for many windows becomes the new normal */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);
    for (i = 0; i < 10 * ATT_APP_PLATFORM_BASELINE_WINDOW; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO - 3);
        UtAssert_UINT32_EQ(UT_HEALTH_Step(), 0);
    }
    UtAssert_True(UT_State.MeanBaseline < (float)(UT_HEALTH_NOMINAL_CNO - 3) + 0.01f,
                  "Baseline followed to %.3f",
                  (double)UT_State.MeanBaseline);
    UtAssert_True(UT_State.Sats[0].Baseline < (float)(UT_HEALTH_NOMINAL_CNO - 3) + 0.01f,
                  "Satellite baseline followed to %.3f",
                  (double)UT_State.Sats[0].Baseline);

    /* A satellite that sets keeps its entry for a while, then gives it up */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);
    for (i = 0; i < ATT_APP_PLATFORM_SAT_EXPIRE_EPOCHS; ++i)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
        UT_Epoch.NumSats = UT_HEALTH_NUM_SATS - 1; /* PRN 30 gone */
        UT_HEALTH_Step();
    }
    UtAssert_UINT32_EQ(UT_Result.BaselinedSats, UT_HEALTH_NUM_SATS);
    UtAssert_UINT32_EQ(UT_Result.DroppedSats, 1);
    UT_HEALTH_Step();
    UT_HEALTH_Step();
    UtAssert_UINT32_EQ(UT_Result.BaselinedSats, UT_HEALTH_NUM_SATS - 1);
    UtAssert_UINT32_EQ(UT_Result.DroppedSats, 0);

    /* The sample counters saturate instead of wrapping back to "untrusted" */
    UT_State.MeanSamples     = 65535;
    UT_State.Sats[0].Samples = 65535;
    UT_HEALTH_Step();
    UtAssert_UINT32_EQ(UT_State.MeanSamples, 65535);
    UtAssert_UINT32_EQ(UT_State.Sats[0].Samples, 65535);

    /* The missed-epoch counter saturates as well (expiry disabled by using the largest limit) */
    UT_HEALTH_Setup();
    UT_HEALTH_RunNominal(UT_HEALTH_WARMUP);
    UT_Config.SatExpireEpochs     = 255;
    UT_State.Sats[9].MissedEpochs = 255;
    UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);
    UT_Epoch.NumSats = UT_HEALTH_NUM_SATS - 1; /* PRN 30 absent */
    UT_HEALTH_Step();
    UtAssert_UINT32_EQ(UT_State.Sats[9].MissedEpochs, 255);
    UtAssert_UINT32_EQ(UT_State.Sats[9].Prn, 30);

    /*
     * Table full (12 entries) and a satellite never seen before appears in
     * place of PRN 1. The newcomer takes over the entry of the satellite
     * that has been gone longest, here PRN 1 itself; no other entry is touched.
     */
    UT_HEALTH_Setup();
    memset(&UT_Epoch, 0, sizeof(UT_Epoch));
    UT_Epoch.Gga.FixQuality = 1;
    UT_Epoch.NumSats        = ATT_APP_MISSION_MAX_SATS;
    for (i = 0; i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        UT_Epoch.Sats[i].Prn = (uint8)(i + 1);
        UT_Epoch.Sats[i].Cno = UT_HEALTH_NOMINAL_CNO;
    }
    UT_HEALTH_Step();
    UT_Epoch.Sats[0].Prn = 99;
    UT_HEALTH_Step();
    Found = 0;
    for (i = 0; i < ATT_APP_MISSION_MAX_SATS; ++i)
    {
        Found += (UT_State.Sats[i].Prn == 99) ? 1 : 0;
        Found += (UT_State.Sats[i].Prn == 1) ? 100 : 0;
    }
    UtAssert_INT32_EQ(Found, 1);
    UtAssert_UINT32_EQ(UT_State.Sats[1].Prn, 2);
    UtAssert_UINT32_EQ(UT_State.Sats[1].Samples, 2);

    /* A satellite with PRN 0 (not a valid ID) never gets an entry */
    UT_HEALTH_Setup();
    memset(&UT_Epoch, 0, sizeof(UT_Epoch));
    UT_Epoch.NumSats     = 1;
    UT_Epoch.Sats[0].Cno = UT_HEALTH_NOMINAL_CNO;
    UT_HEALTH_Step();
    UtAssert_UINT32_EQ(UT_State.Sats[0].Samples, 0);
}

void Test_ATT_APP_HEALTH_SampleLogScenario(void)
{
    /*
     * Test Case For:
     * void ATT_APP_HEALTH_Update()
     *
     * The three scripted events of the sample log, in order, with the
     * flags checked at every epoch: each flag sets and clears in its own
     * window and never in another one's - except LOW_CNO_SAT during the
     * broadband drop, which is correct: jammed satellites are also weak.
     */
    uint32 t;
    uint8  Flags;
    uint8  Expected;
    uint32 Raises[3] = { 0, 0, 0 };
    uint32 Clears[3] = { 0, 0, 0 };
    uint32 Bit;

    UT_HEALTH_Setup();

    for (t = 0; t < 300; ++t)
    {
        UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO);

        if (t >= 60 && t < 100)
        {
            UT_Epoch.Sats[7].Cno = 27; /* PRN 21 fades alone */
        }
        if (t >= 150 && t < 180)
        {
            UT_HEALTH_NominalEpoch(UT_HEALTH_NOMINAL_CNO - 12); /* everything falls */
        }
        if (t >= 230 && t < 240)
        {
            UT_Epoch.Gga.FixQuality = ATT_APP_NMEA_FIX_QUALITY_NONE;
            UT_Epoch.Sats[1].Cno = UT_Epoch.Sats[3].Cno = UT_Epoch.Sats[4].Cno = UT_Epoch.Sats[5].Cno = 0;
            UT_Epoch.Sats[7].Cno = UT_Epoch.Sats[8].Cno = UT_Epoch.Sats[9].Cno = 0;
        }

        Flags = UT_HEALTH_Step();

        for (Bit = 0; Bit < 3; ++Bit)
        {
            Raises[Bit] += (UT_Result.Raised >> Bit) & 1;
            Clears[Bit] += (UT_Result.Cleared >> Bit) & 1;
        }

        Expected = 0;
        if (t >= 60 && t < 100)
        {
            Expected |= UT_LOW;
        }
        if (t >= 150 && t < 180)
        {
            Expected |= UT_LOW; /* 31 dB-Hz is inside the band, 31 < 30 is false: see below */
        }
        if (t >= 151 && t < 182)
        {
            Expected |= UT_DROP; /* set on the 2nd epoch, clear on the 3rd good one */
        }
        if (t >= 230 && t < 241)
        {
            Expected |= UT_NO_FIX; /* clear on the 2nd good epoch */
        }

        /* 43 - 12 = 31 dB-Hz is above the 30 dB-Hz threshold: jamming at this depth is not "weak satellite" */
        if (t >= 150 && t < 180)
        {
            Expected &= (uint8)~UT_LOW;
        }

        UtAssert_True(Flags == Expected,
                      "t=%u flags 0x%02x expected 0x%02x",
                      (unsigned int)t,
                      (unsigned int)Flags,
                      (unsigned int)Expected);
    }

    /* Edge triggered: exactly one set and one clear per event over the whole log */
    for (Bit = 0; Bit < 3; ++Bit)
    {
        UtAssert_UINT32_EQ(Raises[Bit], 1);
        UtAssert_UINT32_EQ(Clears[Bit], 1);
    }
}

/*
 * Register the test cases to execute with the unit test tool
 */
void UtTest_Setup(void)
{
    ADD_TEST(ATT_APP_HEALTH_Debounce);
    ADD_TEST(ATT_APP_HEALTH_NullAndInit);
    ADD_TEST(ATT_APP_HEALTH_Nominal);
    ADD_TEST(ATT_APP_HEALTH_NoFix);
    ADD_TEST(ATT_APP_HEALTH_LowCno);
    ADD_TEST(ATT_APP_HEALTH_BroadbandDrop);
    ADD_TEST(ATT_APP_HEALTH_DropNotTripped);
    ADD_TEST(ATT_APP_HEALTH_Baselines);
    ADD_TEST(ATT_APP_HEALTH_SampleLogScenario);
}
