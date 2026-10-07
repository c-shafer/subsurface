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

/* 12/6 backgas breaks + 100% O2 used to re-read the full cached hold
 * every chunk and never leave the stop. 100/60 VVal-79 with O2 from
 * 30 fsw is long enough to hit the 12 min chop. */
void TestThalmann::testBackgasBreaksDoNotHang()
{
	deco_state_cache cache;
	std::vector<decostop> stoptable;
	setupThalmannPrefs(0, 75);
	prefs.doo2breaks = true;
	prefs.min_switch_duration = 0;

	dive.dcs[0].divemode = OC;
	struct gasmix air = { 21_percent, 0_percent };
	struct gasmix o2 = { 100_percent, 0_percent };
	cylinder_t *cyl0 = dive.get_or_create_cylinder(0);
	cyl0->gasmix = air;
	cyl0->type.size = 36_l;
	cyl0->type.workingpressure = 232_bar;
	cyl0->cylinder_use = OC_GAS;
	cylinder_t *cyl1 = dive.get_or_create_cylinder(1);
	cyl1->gasmix = o2;
	cyl1->type.size = 11_l;
	cyl1->type.workingpressure = 200_bar;
	cyl1->cylinder_use = OC_GAS;
	cyl1->depth = m_or_ft(0, 30);
	for (size_t i = 2; i < dive.cylinders.size(); i++)
		dive.cylinders[i].cylinder_use = NOT_USED;
	reset_cylinders(&dive, true);

	diveplan dp;
	dp.salinity = USN_SALINITY;
	dp.surface_pressure = 1_atm;
	dp.gfhigh = 100;
	dp.gflow = 100;
	dp.bottomsac = prefs.bottomsac;
	dp.decosac = prefs.decosac;
	plan_add_segment(dp, 0, m_or_ft(0, 30), 1, 0, true, OC);
	int droptime = 100 * 60 / 75;
	plan_add_segment(dp, droptime, m_or_ft(0, 100), 0, 0, true, OC);
	plan_add_segment(dp, 60 * 60 - droptime, m_or_ft(0, 100), 0, 0, true, OC);

	planner_error_t err = plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);
	QCOMPARE(err, PLAN_OK);
	int t20 = stopSeconds(stoptable, feet_to_mm(20));
	QVERIFY(t20 > 0);
	QVERIFY(t20 < 48 * 3600);
	int chunks12 = 0, chunks6 = 0;
	for (auto &s: stoptable) {
		if (s.time == 12 * 60)
			chunks12++;
		if (s.time == 6 * 60)
			chunks6++;
	}
	QVERIFY(chunks12 > 0);
	QVERIFY(chunks6 > 0);
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

/* Planner ATM, altitude (via mbar), and water density must change VVal-79
 * stop time. 1.027 kg/L + 1013 mbar is the Navy identity. */
void TestThalmann::testSalinityAndAtmAffectSchedule()
{
	setupThalmannPrefs(0, 75);
	setupVval79AirCylinders(false);

	auto run = [](int salinity, int mbar) {
		deco_state_cache cache;
		std::vector<decostop> stoptable;
		auto dp = makeVval79Plan(220, 40, false);
		dp.salinity = salinity;
		dp.surface_pressure.mbar = mbar;
		plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);
		return dp.navy_ee.tdt_seconds;
	};

	int navy = run(10270, 1013);
	int dense = run(11000, 1013);
	int fresh = run(10000, 1013);
	int altitude = run(10270, 800);

	QVERIFY(dense > navy);
	QVERIFY(fresh < navy);
	QVERIFY(altitude < navy);
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

/* TR 12-01 Appendix D 300/4 AIR first stop 30 fsw; 300/5 AIR first stop 40.
 * Leave-bottom IAD is already 20; the walker must recompute IAD after
 * each DINC hop or those stops are skipped. */
void TestThalmann::testVval79BounceFirstStop()
{
	auto first_stop_mm = [](const std::vector<decostop> &table) {
		int deepest = 0;
		for (auto &stop: table)
			if (stop.time && stop.depth > deepest)
				deepest = stop.depth;
		return deepest;
	};

	setupThalmannPrefs(0, 75);
	setupVval79AirCylinders(false);

	{
		deco_state_cache cache;
		std::vector<decostop> stoptable;
		auto dp = makeVval79Plan(300, 4, false);
		dp.salinity = USN_SALINITY;
		plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);
		QCOMPARE(first_stop_mm(stoptable), feet_to_mm(30));
		QVERIFY(stopSeconds(stoptable, feet_to_mm(20)) > 0);
	}
	{
		deco_state_cache cache;
		std::vector<decostop> stoptable;
		auto dp = makeVval79Plan(300, 5, false);
		dp.salinity = USN_SALINITY;
		plan(&test_deco_state, dp, &dive, 0, 60, cache, true, true, &stoptable);
		QCOMPARE(first_stop_mm(stoptable), feet_to_mm(40));
		QVERIFY(stopSeconds(stoptable, feet_to_mm(30)) > 0);
		QVERIFY(stopSeconds(stoptable, feet_to_mm(20)) > 0);
	}
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

QTEST_GUILESS_MAIN(TestThalmann)
