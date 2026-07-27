// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
/* Tests for the LEM probabilistic planner mode (deco mode LEM).
 *
 * These are property tests, not table comparisons, and deliberately so.
 * There is no published LEM schedule to compare against: TR 02-10 does not
 * print LEM-generated tables, it maps LEM onto a deterministic model and
 * prints those (report section 2.2.1). What the backend promises is a
 * contract, and that is what is checked here:
 *
 *   - a generated schedule actually meets the target risk it was planned to,
 *     scored independently by core/lemscore.cpp at full quadrature (the
 *     planner searches at a coarser step, so this is a real check, not a
 *     tautology);
 *   - both parameter sets do this, each against its own calibration;
 *   - a lower target buys more decompression, never less;
 *   - a dive already under target gets no stops;
 *   - a dive where risk is NOT monotone in stop time still terminates with a
 *     finite schedule, which is the whole point of the "does waiting still
 *     help?" half of the ascent criterion in lemplanner.cpp.
 *
 * The dives are 1.3 ata PO2 closed-circuit He-O2, because that is what both
 * parameter sets are calibrated for (MK 16 MOD 1). See lemplanner.cpp.
 */
#include "testlemplanner.h"
#include "core/deco.h"
#include "core/dive.h"
#include "core/errorhelper.h"
#include "core/lemplanner.h"
#include "core/lemscore.h"
#include "core/planner.h"
#include "core/pref.h"
#include "core/subsurfacestartup.h"
#include "core/units.h"

#define DEBUG 1

static struct dive dive;
static struct deco_state test_deco_state;

/* U.S. Navy convention: constant 30 fsw/min ascent, imperial stop grid,
 * last stop at 20 ft, as in testthalmann.cpp. */
static void setupLemPrefs(int paramset, int target_pdcs_hundredths_percent)
{
	prefs = default_prefs;
	prefs.unit_system = IMPERIAL;
	prefs.units.length = units::FEET;
	prefs.ascrate50 = feet_to_mm(30) / 60;
	prefs.ascrate75 = prefs.ascrate50;
	prefs.ascratestops = prefs.ascrate50;
	prefs.ascratelast6m = prefs.ascrate50;
	prefs.descrate = feet_to_mm(60) / 60;
	prefs.last_stop = true;
	prefs.planner_deco_mode = LEM;
	prefs.planner_lem_paramset = paramset;
	prefs.planner_lem_target_pdcs = target_pdcs_hundredths_percent;
}

static diveplan makeDiveplan()
{
	diveplan dp;
	dp.salinity = 10300;
	dp.surface_pressure = 1_atm;
	dp.gfhigh = 100;
	dp.gflow = 100;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;
	return dp;
}

/* Square 1.3 ata PO2 closed-circuit dive on a He-O2 diluent, the regime both
 * parameter sets were fitted to. Bottom time includes descent, matching the
 * reference implementation's convention. */
static planner_error_t planSquareCcrDive(int depth_ft, int bottom_minutes, std::vector<decostop> &stoptable)
{
	deco_state_cache cache;
	prefs.dobailout = false;
	dive.dcs[0].divemode = CCR;

	struct gasmix diluent = { 12_percent, 60_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = diluent;
	cyl0->type.size = 3_l;
	cyl0->type.workingpressure = 200_bar;
	cyl0->cylinder_use = DILUENT;
	reset_cylinders(&dive, true);

	diveplan dp = makeDiveplan();
	int droptime = depth_ft * 60 / 60;	// at 60 ft/min, in seconds
	plan_add_segment(dp, droptime, m_or_ft(0, depth_ft), 0, 1317, true, CCR);
	plan_add_segment(dp, bottom_minutes * 60 - droptime, m_or_ft(0, depth_ft), 0, 1317, true, CCR);

	return plan(&test_deco_state, dp, &dive, 0, 60, cache, true, false, &stoptable);
}

/* Open circuit on air - deliberately outside either parameter set's
 * calibration, used only to exercise the non-monotone case. */
static planner_error_t planSquareAirDive(int depth_ft, int bottom_minutes, std::vector<decostop> &stoptable)
{
	deco_state_cache cache;
	dive.dcs[0].divemode = OC;

	struct gasmix air = { 21_percent, 0_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = air;
	cyl0->type.size = 36_l;
	cyl0->type.workingpressure = 232_bar;
	cyl0->cylinder_use = OC_GAS;	// the CCR tests set DILUENT on the same static dive
	reset_cylinders(&dive, true);

	diveplan dp = makeDiveplan();
	int droptime = depth_ft * 60 / 60;
	plan_add_segment(dp, droptime, m_or_ft(0, depth_ft), 0, 0, true, OC);
	plan_add_segment(dp, bottom_minutes * 60 - droptime, m_or_ft(0, depth_ft), 0, 0, true, OC);

	return plan(&test_deco_state, dp, &dive, 0, 60, cache, true, false, &stoptable);
}

static int totalStopSeconds(const std::vector<decostop> &table)
{
	int total = 0;
	for (auto &stop: table)
		total += stop.time;
	return total;
}

#if DEBUG
static void dumpStops(const char *what, const std::vector<decostop> &table)
{
	printf("%s:\n", what);
	for (auto &stop: table)
		if (stop.time)
			printf("  stop %d mm: %d s\n", stop.depth, stop.time);
	printf("  total %d s\n", totalStopSeconds(table));
}
#endif

/* Independently score the dive the planner just built, at the scorer's full
 * 1 s quadrature, under the parameter set the plan was made with. */
static double achievedPdcs(int paramset)
{
	lem_pdcs_result result = calculate_lem_pdcs(dive, 0);
	if (!result.valid)
		return 1.0;
	return paramset == 0 ? result.pdcs_he8n25 : result.pdcs_hen2_2016;
}

/* A schedule generated at a target risk must actually achieve it. */
static void checkMeetsTarget(int paramset)
{
	std::vector<decostop> stoptable;

	setupLemPrefs(paramset, 150);	// 1.50%
	planner_error_t error = planSquareCcrDive(120, 30, stoptable);

#if DEBUG
	dumpStops(lem_paramset_names[paramset], stoptable);
	printf("  achieved pDCS %.4f%% (target %.4f%%)\n",
	       achievedPdcs(paramset) * 100.0, lem_target_pdcs() * 100.0);
#endif

	QCOMPARE(error, PLAN_OK);
	QVERIFY(totalStopSeconds(stoptable) > 0);
	QVERIFY(achievedPdcs(paramset) <= lem_target_pdcs());
}

void TestLemPlanner::testScheduleMeetsTargetHe8n25()
{
	checkMeetsTarget(0);
}

void TestLemPlanner::testScheduleMeetsTargetHeN2_2016()
{
	checkMeetsTarget(1);
}

/* Asking for less risk must never buy less decompression. */
void TestLemPlanner::testLowerTargetGivesMoreDeco()
{
	std::vector<decostop> loose, tight;

	setupLemPrefs(0, 200);		// 2.00%
	planSquareCcrDive(120, 30, loose);

	setupLemPrefs(0, 50);		// 0.50%
	planSquareCcrDive(120, 30, tight);

#if DEBUG
	dumpStops("target 2.00%", loose);
	dumpStops("target 0.50%", tight);
#endif

	QVERIFY(totalStopSeconds(tight) > totalStopSeconds(loose));
}

/* A dive whose direct ascent is already below the target gets no stops. */
void TestLemPlanner::testNoDecoWhenTargetAlreadyMet()
{
	std::vector<decostop> stoptable;

	setupLemPrefs(0, 200);		// 2.00%, well above a 40 ft dive's risk
	planSquareCcrDive(40, 20, stoptable);

#if DEBUG
	dumpStops("40 ft / 20 min at 2.00%", stoptable);
#endif

	QCOMPARE(totalStopSeconds(stoptable), 0);
}

/* Open-circuit air is outside either parameter set's calibration, and it
 * shows: LEM risk is not monotone in stop time there, so a deterministic
 * "wait for the ceiling to clear" walker would never leave the first stop.
 * The criterion in lemplanner.cpp gives up on stops that stop paying, so the
 * planner must still return a finite schedule rather than running the stop
 * search out to its 48 h timeout. */
void TestLemPlanner::testNonMonotonicRiskTerminates()
{
	std::vector<decostop> stoptable;

	setupLemPrefs(0, 1);		// 0.01% - unreachable on air at 150 ft
	planner_error_t error = planSquareAirDive(150, 40, stoptable);

#if DEBUG
	dumpStops("air, unreachable target", stoptable);
	printf("  achieved pDCS %.4f%%\n", achievedPdcs(0) * 100.0);
#endif

	QCOMPARE(error, PLAN_OK);
	QVERIFY(totalStopSeconds(stoptable) < 48 * 3600);
}

/* A large exposure, where the stop time has to be spread across depths.
 *
 * This is the case that caught the original local ascent criterion: it left
 * every deep stop almost at once, parked at 20 ft where P_DCS saturates, and
 * surfaced at 4.63% against a 2.30% target after 353 min of stops - while
 * libdeco's own global greedy meets the target in 262 min. A schedule that
 * misses the target here while spending MORE time than the reference means
 * the allocation has gone local again.
 *
 * Reference: libdeco lem_generate_schedule() on the same dive, target 2.30%:
 *   80/1 70/2 60/3 50/4 40/3 30/69 20/180, total 262 min, P_DCS 2.2986%. */
void TestLemPlanner::testLargeExposureAllocatesDeepStops()
{
	std::vector<decostop> stoptable;

	setupLemPrefs(0, 230);		// 2.30%
	planner_error_t error = planSquareCcrDive(170, 100, stoptable);

	int deep = 0;	// stop time deeper than the last two stops
	for (auto &stop: stoptable)
		if (stop.depth > feet_to_mm(20))
			deep += stop.time;

#if DEBUG
	dumpStops("170 ft / 100 min CCR, target 2.30%", stoptable);
	printf("  deep (>20 ft) stop time %d s\n", deep);
	printf("  achieved pDCS %.4f%% (target %.4f%%)\n",
	       achievedPdcs(0) * 100.0, lem_target_pdcs() * 100.0);
#endif

	QCOMPARE(error, PLAN_OK);
	QVERIFY(achievedPdcs(0) <= lem_target_pdcs());
	/* The reference puts 82 of its 262 min above the last stop. A schedule
	 * that reaches the target with essentially nothing deep would mean the
	 * allocation collapsed to the last stop again. */
	QVERIFY(deep >= 20 * 60);
	/* And it must not pay for the target with a wildly longer schedule
	 * than the reference needs. */
	QVERIFY(totalStopSeconds(stoptable) < 2 * 262 * 60);
}

QTEST_GUILESS_MAIN(TestLemPlanner)
