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
 * Stop times are taken from libdeco's generate_ascent_schedule(), so
 * whole-minute holds should match that implementation. Remaining slack
 * is the planner's isobaric "drop to first depth" versus libdeco's
 * ramped descent.
 */
#include "testthalmann.h"
#include "core/deco.h"
#include "core/dive.h"
#include "core/errorhelper.h"
#include "core/planner.h"
#include "core/pref.h"
#include "core/thalmann.h"
#include "core/bvm3.h"
#include "core/divelist.h"
#include "core/divecomputer.h"
#include "core/sample.h"
#include "core/subsurfacestartup.h"
#include "core/units.h"

#include <memory>

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

/* 200 fsw / 40 min CCR 1.3 atm, XVal-He-9_023.
 * ccr_example holds: 90:2 80:3 70:3 60:4 50:10 40:10 30:11 20:98 (total 141).
 * wait_until used to pad ~0.6 min per stop; live Navy holds should not. */
void TestThalmann::testCcr200fsw40min()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;

	setupThalmannPrefs(3, 60);
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

	int droptime = 200 * 60 / 60;
	plan_add_segment(dp, droptime, m_or_ft(0, 200), 0, 1317, true, CCR);
	plan_add_segment(dp, 40 * 60 - droptime, m_or_ft(0, 200), 0, 1317, true, CCR);

	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, false, &stoptable);

#if DEBUG
	for (auto &stop: stoptable)
		if (stop.time)
			printf("stop %d mm: %d s (%.1f min)\n", stop.depth, stop.time, stop.time / 60.0);
#endif

	int t90 = stopSeconds(stoptable, feet_to_mm(90));
	QVERIFY(abs(t90 - 2 * 60) <= 5);
	int total = totalStopSeconds(stoptable);
	QVERIFY(abs(total - 141 * 60) <= 3 * 60);
}

void TestThalmann::testLiveRecomputeAfterExtraDwell()
{
	setupThalmannPrefs(3, 60);
	prefs.dobailout = false;
	dive.dcs[0].divemode = CCR;
	dive.surface_pressure = 1_atm;
	struct gasmix diluent = { 12_percent, 60_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = diluent;
	cyl0->cylinder_use = DILUENT;
	reset_cylinders(&dive, true);

	clear_deco(&test_deco_state, dive.surface_pressure.mbar / 1000.0, true);
	double p200 = dive.depth_to_bar(m_or_ft(0, 200));
	add_segment(&test_deco_state, p200, diluent, 40 * 60, 1317, CCR, prefs.bottomsac, true);

	depth_t at = m_or_ft(0, 90);
	depth_t nxt = m_or_ft(0, 80);
	int h1 = thalmann_hold_seconds_for_next_stop(&test_deco_state, at, nxt, diluent, 1317, CCR,
						    prefs.ascratestops);
	QVERIFY(h1 > 0);
	add_segment(&test_deco_state, dive.depth_to_bar(at), diluent, 10 * 60, 1317, CCR, prefs.decosac, true);
	int h2 = thalmann_hold_seconds_for_next_stop(&test_deco_state, at, nxt, diluent, 1317, CCR,
						    prefs.ascratestops);
	QVERIFY(h1 != h2);
}

/* Over M0, the profile ceiling is the Navy last stop (20 fsw), not the
 * old lastop-dinc/2 = 15 fsw trial-ascent workaround. Occupying 20 ft
 * is still allowed because the walker uses hold seconds, not ceiling. */
void TestThalmann::testLastStopCeilingIsSetM()
{
	setupThalmannPrefs(0, 75);
	dive.dcs[0].divemode = OC;
	dive.surface_pressure = 1_atm;
	struct gasmix air = { 21_percent, 0_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = air;
	cyl0->cylinder_use = OC_GAS;
	reset_cylinders(&dive, true);

	clear_deco(&test_deco_state, dive.surface_pressure.mbar / 1000.0, true);
	add_segment(&test_deco_state, dive.depth_to_bar(m_or_ft(0, 100)), air,
		    60 * 60, 0, OC, prefs.bottomsac, true);

	double surface = dive.surface_pressure.mbar / 1000.0;
	double amb = thalmann_tolerated_ambient_pressure(&test_deco_state, surface);
	double gauge_fsw = (amb - surface) * 33.0 / 1.01325;
	QVERIFY(gauge_fsw >= 19.5);
	QVERIFY(gauge_fsw < 25.0);

	depth_t at20 = m_or_ft(0, 20);
	depth_t surf = 0_m;
	int hold = thalmann_hold_seconds_for_next_stop(&test_deco_state, at20, surf, air, 0, OC,
						      prefs.ascratestops);
	QVERIFY(hold > 0);
	QVERIFY(!thalmann_trial_ascent(&test_deco_state, at20, surf, air, 0, OC, prefs.ascratestops));
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

/* 100 fsw / 100 min air, switch to 80% O2 at 30 ft — mixed_gas_example's
 * default deco_legs case. libdeco: 40:8, 30:14, 20:65. */
void TestThalmann::testOpenCircuitAirO2()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;

	setupThalmannPrefs(0, 75);	// VVal-79, 75 fsw/min descent
	prefs.switch_at_req_stop = true;
	dive.dcs[0].divemode = OC;

	diveplan dp;
	dp.salinity = 10300;
	dp.surface_pressure = 1_atm;
	dp.gfhigh = 100;
	dp.gflow = 100;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;

	struct gasmix air = { 21_percent, 0_percent };
	struct gasmix ean80 = { 80_percent, 0_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = air;
	cyl0->type.size = 36_l;
	cyl0->type.workingpressure = 232_bar;
	cyl0->cylinder_use = OC_GAS;
	cylinder_t *cyl1 = dive.get_or_create_cylinder(1);
	cyl1->gasmix = ean80;
	cyl1->type.size = 11_l;
	cyl1->type.workingpressure = 200_bar;
	cyl1->cylinder_use = OC_GAS;
	cyl1->depth = m_or_ft(0, 30);
	reset_cylinders(&dive, true);

	plan_add_segment(dp, 0, m_or_ft(0, 30), 1, 0, true, OC);
	int droptime = 100 * 60 / 75;
	plan_add_segment(dp, droptime, m_or_ft(0, 100), 0, 0, true, OC);
	plan_add_segment(dp, 100 * 60 - droptime, m_or_ft(0, 100), 0, 0, true, OC);

	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, false, &stoptable);

#if DEBUG
	dive.notes.clear();
	save_dive(stdout, dive, false);
	for (auto &stop: stoptable)
		if (stop.time)
			printf("stop %d mm: %d s\n", stop.depth, stop.time);
#endif

	int t40 = stopSeconds(stoptable, feet_to_mm(40));
	int t30 = stopSeconds(stoptable, feet_to_mm(30));
	int t20 = stopSeconds(stoptable, feet_to_mm(20));
	// 40 ft can pick up ~1-2 min from the planner's isobaric drop vs
	// libdeco's ramped descent; 30/20 should match the example.
	QVERIFY(abs(t40 - 8 * 60) <= 2 * 60);
	QVERIFY(abs(t30 - 14 * 60) <= 60);
	QVERIFY(abs(t20 - 65 * 60) <= 60);
}

/* 100 fsw / 30 min air, VVal-79. Thalmann stops are shorter than a
 * Bühlmann GF 30/75 schedule, so the Bühlmann surfacing GF should
 * exceed GFHigh and the plan notes should warn. */
void TestThalmann::testSurfacingGfWarning()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;

	setupThalmannPrefs(0, 75);	// VVal-79, 75 fsw/min descent
	dive.dcs[0].divemode = OC;

	diveplan dp;
	dp.salinity = 10300;
	dp.surface_pressure = 1_atm;
	dp.gfhigh = 75;
	dp.gflow = 30;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;

	struct gasmix air = { 21_percent, 0_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = air;
	cyl0->type.size = 36_l;
	cyl0->type.workingpressure = 232_bar;
	cyl0->cylinder_use = OC_GAS;
	for (size_t i = 1; i < dive.cylinders.size(); i++)
		dive.cylinders[i].cylinder_use = NOT_USED;
	reset_cylinders(&dive, true);

	int droptime = 100 * 60 / 75;
	plan_add_segment(dp, droptime, m_or_ft(0, 100), 0, 0, true, OC);
	plan_add_segment(dp, 30 * 60 - droptime, m_or_ft(0, 100), 0, 0, true, OC);

	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);

	QVERIFY(dp.surface_gf > 75.0);
	QVERIFY(dive.notes.find("Surfacing GF (Bühlmann)") != std::string::npos);
	QVERIFY(dive.notes.find("Warning:") != std::string::npos);
	QVERIFY(dive.notes.find("higher than the selected GFHigh") != std::string::npos);
}

static void setupVval79AirCylinders(bool ean80)
{
	dive.dcs[0].divemode = OC;
	struct gasmix air = { 21_percent, 0_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = air;
	cyl0->type.size = 36_l;
	cyl0->type.workingpressure = 232_bar;
	cyl0->cylinder_use = OC_GAS;
	if (ean80) {
		struct gasmix mix80 = { 80_percent, 0_percent };
		cylinder_t *cyl1 = dive.get_or_create_cylinder(1);
		cyl1->gasmix = mix80;
		cyl1->type.size = 11_l;
		cyl1->type.workingpressure = 200_bar;
		cyl1->cylinder_use = OC_GAS;
		cyl1->depth = m_or_ft(0, 30);
		for (size_t i = 2; i < dive.cylinders.size(); i++)
			dive.cylinders[i].cylinder_use = NOT_USED;
	} else {
		for (size_t i = 1; i < dive.cylinders.size(); i++)
			dive.cylinders[i].cylinder_use = NOT_USED;
	}
	reset_cylinders(&dive, true);
}

static diveplan makeVval79Plan(int depth_ft, int bottom_min, bool ean80)
{
	diveplan dp;
	dp.salinity = 10300;
	dp.surface_pressure = 1_atm;
	dp.gfhigh = 100;
	dp.gflow = 100;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;
	if (ean80)
		plan_add_segment(dp, 0, m_or_ft(0, 30), 1, 0, true, OC);
	int droptime = depth_ft * 60 / 75;
	int bottom = bottom_min * 60;
	if (droptime > bottom)
		droptime = bottom;
	plan_add_segment(dp, droptime, m_or_ft(0, depth_ft), 0, 0, true, OC);
	plan_add_segment(dp, bottom - droptime, m_or_ft(0, depth_ft), 0, 0, true, OC);
	return dp;
}

/* 100 fsw / 60 min air: table TAT 84:20, under the 90 min air cut. */
void TestThalmann::testExceptionalExposureAir60()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;
	setupThalmannPrefs(0, 75);
	setupVval79AirCylinders(false);
	auto dp = makeVval79Plan(100, 60, false);
	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);

	QVERIFY(dp.navy_ee.applicable);
	QVERIFY(!dp.navy_ee.air_o2);
	QVERIFY(dp.navy_ee.tdt_seconds <= 90 * 60);
	QVERIFY(!dp.navy_ee.exceptional);
	QVERIFY(dive.notes.find("Navy TDT") != std::string::npos);
	QVERIFY(dive.notes.find("in-water air") != std::string::npos);
	QVERIFY(dive.notes.find("exceptional-exposure") == std::string::npos);
}

/* 100 fsw / 70 min air: table TAT 138:00, EE for in-water air. */
void TestThalmann::testExceptionalExposureAir70()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;
	setupThalmannPrefs(0, 75);
	setupVval79AirCylinders(false);
	auto dp = makeVval79Plan(100, 70, false);
	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);

	QVERIFY(dp.navy_ee.applicable);
	QVERIFY(!dp.navy_ee.air_o2);
	QVERIFY(dp.navy_ee.tdt_seconds > 90 * 60);
	QVERIFY(dp.navy_ee.exceptional);
	QCOMPARE(dp.navy_ee.reason, diveplan::NAVY_EE_AIR_TDT);
	QVERIFY(dive.notes.find("in-water air TDT") != std::string::npos);
	QVERIFY(dive.notes.find("In-water Air/O") != std::string::npos);
}

/* 100 fsw / 70 min, EAN80 at 30 ft: table TAT 53:00, not EE. */
void TestThalmann::testExceptionalExposureAirO2_70()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;
	setupThalmannPrefs(0, 75);
	prefs.switch_at_req_stop = true;
	setupVval79AirCylinders(true);
	auto dp = makeVval79Plan(100, 70, true);
	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);

	QVERIFY(dp.navy_ee.applicable);
	QVERIFY(dp.navy_ee.air_o2);
	QVERIFY(dp.navy_ee.tdt_seconds <= 90 * 60);
	QVERIFY(!dp.navy_ee.exceptional);
	QVERIFY(dive.notes.find("in-water Air/O") != std::string::npos);
	QVERIFY(dive.notes.find("exceptional-exposure") == std::string::npos);
}

/* 100 fsw / 100 min, EAN80 at 30 ft: table TAT 102:00, EE for Air/O2. */
void TestThalmann::testExceptionalExposureAirO2_100()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;
	setupThalmannPrefs(0, 75);
	prefs.switch_at_req_stop = true;
	setupVval79AirCylinders(true);
	auto dp = makeVval79Plan(100, 100, true);
	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);

	QVERIFY(dp.navy_ee.applicable);
	QVERIFY(dp.navy_ee.air_o2);
	QVERIFY(dp.navy_ee.tdt_seconds > 90 * 60);
	QVERIFY(dp.navy_ee.exceptional);
	QCOMPARE(dp.navy_ee.reason, diveplan::NAVY_EE_AIR_O2_TDT);
	QVERIFY(dive.notes.find("in-water Air/O") != std::string::npos);
	QVERIFY(dive.notes.find("SurDO") != std::string::npos);
}

/* Any air dive deeper than 190 fsw is EE regardless of TDT. */
void TestThalmann::testExceptionalExposureDepth()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;
	setupThalmannPrefs(0, 75);
	setupVval79AirCylinders(false);
	auto dp = makeVval79Plan(200, 5, false);
	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);

	QVERIFY(dp.navy_ee.applicable);
	QVERIFY(dp.navy_ee.max_depth_fsw > 190.0);
	QVERIFY(dp.navy_ee.exceptional);
	QCOMPARE(dp.navy_ee.reason, diveplan::NAVY_EE_DEPTH);
	QVERIFY(dive.notes.find("exceeds 190 fsw") != std::string::npos);
}

/* A time-0 gas-list waypoint at 240 ft (cylinder MOD) must not make a
 * 100 ft dive look deeper than 190 fsw. 100/100 air is EE on TDT. */
void TestThalmann::testExceptionalExposureIgnoresGasModWaypoint()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;
	setupThalmannPrefs(0, 75);
	setupVval79AirCylinders(false);
	auto dp = makeVval79Plan(100, 100, false);
	/* Gas-list MOD marker: duration 0, inserted at the start (time stays 0). */
	dp.dp.insert(dp.dp.begin(), divedatapoint(0, m_or_ft(0, 240), 0, 0, true));
	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);

	QVERIFY(dp.navy_ee.max_depth_fsw < 110.0);
	QVERIFY(dp.navy_ee.tdt_seconds > 90 * 60);
	QVERIFY(dp.navy_ee.exceptional);
	QCOMPARE(dp.navy_ee.reason, diveplan::NAVY_EE_AIR_TDT);
	QVERIFY(dive.notes.find("exceeds 190 fsw") == std::string::npos);
	QVERIFY(dive.notes.find("in-water air TDT") != std::string::npos);
}

/* Gerth & Vann 1997 Fig. 3: 60 fsw / 60 min no-stop is 1.80% P(DCS).
 * Navy 75/30 fsw/min square; oai_bvm3.py scores this 1.798%. */
void TestThalmann::testBvm3NoStop60()
{
	std::vector<bvm3_node> nodes;
	nodes.push_back({ 0.0, 0.0, 0.79 });
	nodes.push_back({ 60.0 / 75.0, 60.0, 0.79 });
	nodes.push_back({ 60.0, 60.0, 0.79 });
	nodes.push_back({ 60.0 + 60.0 / 30.0, 0.0, 0.79 });
	double p = bvm3_score_nodes(nodes);
	QVERIFY(p > 0.015);
	QVERIFY(p < 0.022);
}

void TestThalmann::testBvm3NotesOnVval79()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;
	setupThalmannPrefs(0, 75);
	setupVval79AirCylinders(false);
	auto dp = makeVval79Plan(100, 30, false);
	plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);
	QVERIFY(dive.notes.find("pDCS (BVM(3)") != std::string::npos);
	QVERIFY(dive.notes.find("Gerth") != std::string::npos);
}

void TestThalmann::testBvm3VsAppendixE()
{
	struct row {
		int depth_ft, bt_min;
		int app_e_air_stop_min;
		double app_e_air_pct;
	};
	const row rows[] = {
		{ 60, 63, 0, 1.723 },
		{ 90, 33, 0, 2.239 },
		{ 100, 25, 0, 2.119 },
		{ 100, 30, 3, 2.322 },
		{ 100, 60, 81, 5.499 },
		{ 100, 100, 278, 8.753 },
		{ 170, 20, 33, 3.670 },
		{ 170, 180, 1156, 19.122 },
	};
	/* Direct node list matching oai_bvm3.py's Appendix-D 100/30 air
	 * profile (descent inside BT, 3 min at 20 fsw). Python: 2.353%. */
	{
		double descent = 100.0 / 75.0;
		double to20 = 80.0 / 30.0;
		double t0 = 0.0, t1 = descent, t2 = 30.0, t3 = 30.0 + to20;
		double t4 = t3 + 3.0, t5 = t4 + 20.0 / 30.0;
		std::vector<bvm3_node> nodes = {
			{ t0, 0.0, 0.79 }, { t1, 100.0, 0.79 }, { t2, 100.0, 0.79 },
			{ t3, 20.0, 0.79 }, { t4, 20.0, 0.79 }, { t5, 0.0, 0.79 },
		};
		double p = bvm3_score_nodes(nodes) * 100.0;
		printf("100/30 node list  AppE 2.322%%  SS %.3f%%\n", p);
		QVERIFY(p > 2.20);
		QVERIFY(p < 2.50);
	}
	/* oai_bvm3.py 100/100 AIR/O2: last in-water gas is 99.5% O2, but
	 * post-surface breathing is air. Python: 3.837% vs AppE 3.793%.
	 * Continuing O2 at the surface scores ~1.85%. */
	{
		const double air = 0.79, o2 = 0.005;
		double t = 0.0;
		std::vector<bvm3_node> nodes = { { 0.0, 0.0, air } };
		auto stage = [&](double dur, double depth1, double fn2) {
			if (std::fabs(nodes.back().fn2 - fn2) > 1e-12) {
				t += 1e-6;
				nodes.push_back({ t, nodes.back().depth_fsw, fn2 });
			}
			t += dur;
			nodes.push_back({ t, depth1, fn2 });
		};
		stage(100.0 / 75.0, 100.0, air);
		stage(100.0 - 100.0 / 75.0, 100.0, air);
		stage(2.0, 40.0, air);
		stage(9.0, 40.0, air);
		stage(10.0 / 30.0, 30.0, air);
		stage(14.0, 30.0, o2);
		stage(10.0 / 30.0, 20.0, o2);
		stage(15.0 + 2.0 / 3.0, 20.0, o2);
		stage(5.0, 20.0, air);
		stage(30.0, 20.0, o2);
		stage(5.0, 20.0, air);
		stage(20.0, 20.0, o2);
		stage(20.0 / 30.0, 0.0, o2);
		double p = bvm3_score_nodes(nodes) * 100.0;
		printf("100/100 AIR/O2 nodes  AppE 3.793%%  SS %.3f%%\n", p);
		QVERIFY(p > 3.50);
		QVERIFY(p < 4.20);
	}

	printf("depth/BT  AppE stop  SS stop  AppE BVM(3)  SS BVM(3)\n");
	for (auto &r: rows) {
		deco_state_cache cache;
		std::vector<decostop> stoptable;
		setupThalmannPrefs(0, 75);
		setupVval79AirCylinders(false);
		auto dp = makeVval79Plan(r.depth_ft, r.bt_min, false);
		plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);
		int ss_stop = (totalStopSeconds(stoptable) + 30) / 60;
		bvm3_result b = calculate_bvm3_pdcs(dive, 0);
		double got = b.valid ? b.pdcs * 100.0 : -1.0;
		printf(" %3d/%-3d   %5d      %5d     %6.3f%%     %6.3f%%\n",
		       r.depth_ft, r.bt_min, r.app_e_air_stop_min, ss_stop,
		       r.app_e_air_pct, got);
		QVERIFY(b.valid);
		if (r.depth_ft == 100 && r.bt_min == 30) {
			QVERIFY(got > 2.0);
			QVERIFY(got < 3.0);
		}
	}

	{
		deco_state_cache cache;
		std::vector<decostop> stoptable;
		setupThalmannPrefs(0, 75);
		setupVval79AirCylinders(true);
		auto dp = makeVval79Plan(100, 100, true);
		plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);
		int ss_stop = (totalStopSeconds(stoptable) + 30) / 60;
		bvm3_result b = calculate_bvm3_pdcs(dive, 0);
		double got = b.valid ? b.pdcs * 100.0 : -1.0;
		printf("100/100 planner AIR/O2  stop %d  AppE 3.793%%  SS %.3f%%\n",
		       ss_stop, got);
		QVERIFY(b.valid);
		QVERIFY(got > 2.5);
	}
}

void TestThalmann::testBvm3Repetitive()
{
	auto make_square = [](timestamp_t when) {
		std::unique_ptr<struct dive> d(new struct dive);
		d->when = when;
		d->salinity = 10300;
		d->surface_pressure = 1_atm;
		cylinder_t *cyl = d->get_or_create_cylinder(0);
		cyl->gasmix = { 21_percent, 0_percent };
		struct divecomputer &dc = d->dcs[0];
		dc.divemode = OC;
		struct sample s0, s1, s2, s3;
		s0.time.seconds = 0;
		s0.depth.mm = 0;
		s1.time.seconds = static_cast<int>(60.0 / 75.0 * 60.0);
		s1.depth.mm = feet_to_mm(60);
		s2.time.seconds = 60 * 60;
		s2.depth.mm = feet_to_mm(60);
		s3.time.seconds = 60 * 60 + static_cast<int>(60.0 / 30.0 * 60.0);
		s3.depth.mm = 0;
		append_sample(s0, &dc);
		append_sample(s1, &dc);
		append_sample(s2, &dc);
		append_sample(s3, &dc);
		d->duration.seconds = s3.time.seconds;
		d->maxdepth.mm = feet_to_mm(60);
		return d;
	};

	timestamp_t t0 = 1'700'000'000;
	auto first = make_square(t0);
	auto second = make_square(t0 + first->duration.seconds + 60 * 60);

	dive_table empty;
	bvm3_result iso = calculate_bvm3_pdcs(*second, 0, &empty);
	QVERIFY(iso.valid);
	QCOMPARE(iso.n_prior_dives, 0);

	dive_table table;
	table.put(std::move(first));
	bvm3_result rep = calculate_bvm3_pdcs(*second, 0, &table);
	QVERIFY(rep.valid);
	QCOMPARE(rep.n_prior_dives, 1);
	QVERIFY(rep.pdcs > iso.pdcs);

	auto late = make_square(t0 + 62 * 60 + 49 * 3600);
	bvm3_result gap = calculate_bvm3_pdcs(*late, 0, &table);
	QVERIFY(gap.valid);
	QCOMPARE(gap.n_prior_dives, 0);
}

QTEST_GUILESS_MAIN(TestThalmann)
