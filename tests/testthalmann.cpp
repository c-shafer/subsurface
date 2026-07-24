// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
/* Tests for the Thalmann EL-DCM planner mode (deco mode THALMANN).
 *
 * The reference schedules below were generated with the standalone
 * libdeco implementation this backend is vendored from (the lemdeco
 * repository), whose own output is golden-table validated against
 * NEDU TR 18-05 / TR 12-01:
 *
 *   ccr_example (XVal-He-9_023): 120 fsw, 60 min bottom time,
 *     descent 60 fsw/min, ascent 30 fsw/min
 *       30 fsw : 3 min, 20 fsw : 56 min (total 59 min)
 *
 *   open_circuit_example (VVal-79 air): 100 fsw, 60 min bottom time,
 *     descent 75 fsw/min, ascent 30 fsw/min
 *       20 fsw : 80 min (total 80 min)
 *
 * Subsurface's planner walks stops on its own grid with its own trial
 * ascent logic, so stop-by-stop agreement is expected only within a
 * few minutes, not exactly.
 */
#include "testthalmann.h"
#include "core/deco.h"
#include "core/dive.h"
#include "core/errorhelper.h"
#include "core/planner.h"
#include "core/pref.h"
#include "core/subsurfacestartup.h"
#include "core/units.h"

#define DEBUG 1

static struct dive dive;
static struct deco_state test_deco_state;

/* U.S. Navy convention: constant 30 fsw/min ascent, imperial stop grid,
 * last stop at 20 ft. */
static void setupThalmannPrefs(int paramset, int descent_fsw_per_min)
{
	prefs = default_prefs;
	prefs.unit_system = IMPERIAL;
	prefs.units.length = units::FEET;
	prefs.ascrate50 = feet_to_mm(30) / 60;
	prefs.ascrate75 = prefs.ascrate50;
	prefs.ascratestops = prefs.ascrate50;
	prefs.ascratelast6m = prefs.ascrate50;
	prefs.descrate = feet_to_mm(descent_fsw_per_min) / 60;
	prefs.last_stop = true;
	prefs.planner_deco_mode = THALMANN;
	prefs.planner_thalmann_paramset = paramset;
}

/* Sum of real (nonzero) stop times, in seconds, plus per-stop lookup. */
static int totalStopSeconds(const std::vector<decostop> &table)
{
	int total = 0;
	for (auto &stop: table)
		total += stop.time;
	return total;
}

static int stopSeconds(const std::vector<decostop> &table, int depth_mm)
{
	int total = 0;
	for (auto &stop: table)
		if (stop.depth == depth_mm)
			total += stop.time;
	return total;
}

void TestThalmann::testCcrHeO2()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;

	setupThalmannPrefs(3, 60);	// XVal-He-9_023, 60 fsw/min descent
	prefs.dobailout = false;
	dive.dcs[0].divemode = CCR;

	diveplan dp;
	dp.salinity = 10300;
	dp.surface_pressure = 1_atm;
	dp.gfhigh = 100;
	dp.gflow = 100;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;

	struct gasmix diluent = { 12_percent, 60_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = diluent;
	cyl0->type.size = 3_l;
	cyl0->type.workingpressure = 200_bar;
	cyl0->cylinder_use = DILUENT;
	reset_cylinders(&dive, true);

	// 1.3 atm PO2 setpoint = 1317 mbar; bottom time includes descent,
	// matching the reference implementation's convention.
	int droptime = 120 * 60 / 60;	// 120 ft at 60 ft/min, in seconds
	plan_add_segment(dp, droptime, m_or_ft(0, 120), 0, 1317, true, CCR);
	plan_add_segment(dp, 60 * 60 - droptime, m_or_ft(0, 120), 0, 1317, true, CCR);

	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, false, &stoptable);

#if DEBUG
	dive.notes.clear();
	save_dive(stdout, dive, false);
	for (auto &stop: stoptable)
		if (stop.time)
			printf("stop %d mm: %d s\n", stop.depth, stop.time);
#endif

	// Reference: 30 fsw : 3 min, 20 fsw : 56 min, total 59 min.
	// No stop deeper than 40 ft should be generated.
	for (auto &stop: stoptable)
		if (stop.time)
			QVERIFY(stop.depth <= feet_to_mm(40));
	int t20 = stopSeconds(stoptable, feet_to_mm(20));
	QVERIFY(abs(t20 - 56 * 60) <= 5 * 60);
	int total = totalStopSeconds(stoptable);
	QVERIFY(abs(total - 59 * 60) <= 6 * 60);
}

/* 100 fsw / 100 min bottom time, CCR at 1.3 atm PO2, XVal-He-9_023.
 * This row is directly published in NEDU TR 18-05 (100 fsw block,
 * BT 100): a single stop, 20 fsw : 86 min. Since it is the first (and
 * only) stop, the printed time excludes travel, so it is a pure hold
 * and directly comparable. The vendored reference implementation
 * (lemdeco cpo2_example 100 100 60 30) gives 85 min for the same row. */
void TestThalmann::testCcrHeO2Deep()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;

	setupThalmannPrefs(3, 60);	// XVal-He-9_023, 60 fsw/min descent
	prefs.dobailout = false;
	dive.dcs[0].divemode = CCR;

	diveplan dp;
	dp.salinity = 10300;
	dp.surface_pressure = 1_atm;
	dp.gfhigh = 100;
	dp.gflow = 100;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;

	struct gasmix diluent = { 12_percent, 60_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = diluent;
	cyl0->type.size = 3_l;
	cyl0->type.workingpressure = 200_bar;
	cyl0->cylinder_use = DILUENT;
	reset_cylinders(&dive, true);

	int droptime = 100 * 60 / 60;	// 100 ft at 60 ft/min, in seconds
	plan_add_segment(dp, droptime, m_or_ft(0, 100), 0, 1317, true, CCR);
	plan_add_segment(dp, 100 * 60 - droptime, m_or_ft(0, 100), 0, 1317, true, CCR);

	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, false, &stoptable);

#if DEBUG
	dive.notes.clear();
	save_dive(stdout, dive, false);
	for (auto &stop: stoptable)
		if (stop.time)
			printf("stop %d mm: %d s\n", stop.depth, stop.time);
#endif

	// Reference: single stop, 20 fsw : 86 min (TR 18-05) / 85 min (libdeco).
	for (auto &stop: stoptable)
		if (stop.time)
			QVERIFY(stop.depth <= feet_to_mm(30));
	int t20 = stopSeconds(stoptable, feet_to_mm(20));
	QVERIFY(abs(t20 - 86 * 60) <= 5 * 60);
	int total = totalStopSeconds(stoptable);
	QVERIFY(abs(total - 86 * 60) <= 6 * 60);
}

void TestThalmann::testOpenCircuitAir()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;

	setupThalmannPrefs(0, 75);	// VVal-79, 75 fsw/min descent
	dive.dcs[0].divemode = OC;

	diveplan dp;
	dp.salinity = 10300;
	dp.surface_pressure = 1_atm;
	dp.gfhigh = 100;
	dp.gflow = 100;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;

	struct gasmix air = { 21_percent, 0_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = air;
	cyl0->type.size = 36_l;
	cyl0->type.workingpressure = 232_bar;
	cyl0->cylinder_use = OC_GAS;	// the CCR test above sets DILUENT on the same static dive
	reset_cylinders(&dive, true);

	int droptime = 100 * 60 / 75;	// 100 ft at 75 ft/min, in seconds
	plan_add_segment(dp, droptime, m_or_ft(0, 100), 0, 0, true, OC);
	plan_add_segment(dp, 60 * 60 - droptime, m_or_ft(0, 100), 0, 0, true, OC);

	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, false, &stoptable);

#if DEBUG
	dive.notes.clear();
	save_dive(stdout, dive, false);
	for (auto &stop: stoptable)
		if (stop.time)
			printf("stop %d mm: %d s\n", stop.depth, stop.time);
#endif

	// Reference: single stop, 20 fsw : 80 min.
	for (auto &stop: stoptable)
		if (stop.time)
			QVERIFY(stop.depth <= feet_to_mm(30));
	int t20 = stopSeconds(stoptable, feet_to_mm(20));
	QVERIFY(abs(t20 - 80 * 60) <= 6 * 60);
	int total = totalStopSeconds(stoptable);
	QVERIFY(abs(total - 80 * 60) <= 7 * 60);
}

QTEST_GUILESS_MAIN(TestThalmann)
