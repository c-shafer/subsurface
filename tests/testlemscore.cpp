// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
/* Tests for LEM pDCS scoring of dive profiles (core/lemscore.cpp).
 *
 * The LEM engine itself (core/deco-thalmann/deco/lem.h) is validated
 * upstream in the lemdeco repository; these tests validate Subsurface's
 * profile conversion into it: sample iteration, gas/setpoint resolution,
 * and the depth/pressure unit conversion.
 */
#include "testlemscore.h"
#include "core/dive.h"
#include "core/divecomputer.h"
#include "core/lemscore.h"
#include "core/divelist.h"
#include "core/sample.h"
#include "core/units.h"

#include <memory>

#include "deco/lem.h"
#include "params/lem_he8n25.h"
#include "params/lem_hen2_2016.h"

static struct dive g_dive;

static void addSample(struct divecomputer &dc, int seconds, int mm, int setpoint_mbar = 0)
{
	struct sample s;
	s.time.seconds = seconds;
	s.depth.mm = mm;
	s.setpoint.mbar = setpoint_mbar;
	append_sample(s, &dc);
}

/* Air trapezoid: descend to 60 ft in 2 min, hold to 30 min, ascend in 2 min. */
static void setupAirDive()
{
	g_dive = dive();
	g_dive.salinity = 10300;
	g_dive.surface_pressure = 1_atm;
	cylinder_t *cyl0 = g_dive.get_or_create_cylinder(0);
	cyl0->gasmix = { 21_percent, 0_percent };

	struct divecomputer &dc = g_dive.dcs[0];
	dc.divemode = OC;
	addSample(dc, 0, 0);
	addSample(dc, 2 * 60, feet_to_mm(60));
	addSample(dc, 30 * 60, feet_to_mm(60));
	addSample(dc, 32 * 60, 0);
	g_dive.duration.seconds = 32 * 60;
	g_dive.maxdepth.mm = feet_to_mm(60);
}

void TestLemScore::testOpenCircuitAgainstReference()
{
	setupAirDive();
	lem_pdcs_result res = calculate_lem_pdcs(g_dive, 0);
	QVERIFY(res.valid);

	// Reference: the same profile handed to the engine directly, using
	// the same gauge-pressure depth convention as the adapter.
	const double fsw_per_bar = 33.0 / 1.01325;
	const double surface_bar = g_dive.get_surface_pressure().mbar / 1000.0;
	auto depth_fsw = [&](int mm) {
		return (g_dive.depth_to_bar(depth_t{ .mm = mm }) - surface_bar) * fsw_per_bar;
	};
	deco::LemProfile ref;
	deco::GasMix air{0.21, 0.0};
	ref.push_back({0.0, 0.0, air, std::nullopt, 0.0});
	ref.push_back({2.0, depth_fsw(feet_to_mm(60)), air, std::nullopt, 0.0});
	ref.push_back({30.0, depth_fsw(feet_to_mm(60)), air, std::nullopt, 0.0});
	ref.push_back({32.0, 0.0, air, std::nullopt, 0.0});

	double ref_he8n25 = deco::lem_score(deco::params::lem_he8n25(), ref).p_dcs;
	double ref_hen2 = deco::lem_score(deco::params::lem_hen2_2016(), ref).p_dcs;

	QVERIFY(fabs(res.pdcs_he8n25 - ref_he8n25) < 1e-9);
	QVERIFY(fabs(res.pdcs_hen2_2016 - ref_hen2) < 1e-9);

	// And the values themselves are physically sensible for a modest
	// air dive: nonzero but low single-digit percent.
	QVERIFY(res.pdcs_he8n25 > 0.0 && res.pdcs_he8n25 < 0.06);
	QVERIFY(res.pdcs_hen2_2016 > 0.0 && res.pdcs_hen2_2016 < 0.06);
}

/* A 100 ft / 60 min air dive with a direct ascent must score strictly
 * higher risk than the same dive with 60 min of stops at 20 ft - checked
 * against the LEM-HeN2_2016 calibration, which is the set fit with N2
 * kinetics relevant to an air dive. (The legacy he8n25 set is a He-O2
 * calibration; on a pure-N2 profile its N2 parameters can score a longer
 * shallow-stop dive marginally higher, since the added elapsed time
 * accrues hazard faster than the air stop offgasses it - a property of
 * using that set outside its calibration gas, not a scoring bug.) */
void TestLemScore::testDecoReducesRisk()
{
	g_dive = dive();
	g_dive.salinity = 10300;
	g_dive.surface_pressure = 1_atm;
	cylinder_t *cyl0 = g_dive.get_or_create_cylinder(0);
	cyl0->gasmix = { 21_percent, 0_percent };

	struct divecomputer &dc = g_dive.dcs[0];
	dc.divemode = OC;
	dc.samples.clear();
	addSample(dc, 0, 0);
	addSample(dc, 2 * 60, feet_to_mm(100));
	addSample(dc, 60 * 60, feet_to_mm(100));
	addSample(dc, 63 * 60, 0);
	g_dive.duration.seconds = 63 * 60;
	g_dive.maxdepth.mm = feet_to_mm(100);
	lem_pdcs_result direct = calculate_lem_pdcs(g_dive, 0);
	QVERIFY(direct.valid);

	dc.samples.clear();
	addSample(dc, 0, 0);
	addSample(dc, 2 * 60, feet_to_mm(100));
	addSample(dc, 60 * 60, feet_to_mm(100));
	addSample(dc, 63 * 60, feet_to_mm(20));
	addSample(dc, 123 * 60, feet_to_mm(20));
	addSample(dc, 124 * 60, 0);
	g_dive.duration.seconds = 124 * 60;
	lem_pdcs_result stopped = calculate_lem_pdcs(g_dive, 0);
	QVERIFY(stopped.valid);

	QVERIFY(stopped.pdcs_hen2_2016 < direct.pdcs_hen2_2016);
}

/* CCR at a 1.3 atm setpoint must score lower than open-circuit on the
 * bare diluent for the same profile (less inert gas breathed), and the
 * setpoint path must actually be exercised (differ from the OC score). */
void TestLemScore::testCcrSetpoint()
{
	g_dive = dive();
	g_dive.salinity = 10300;
	g_dive.surface_pressure = 1_atm;
	cylinder_t *cyl0 = g_dive.get_or_create_cylinder(0);
	cyl0->gasmix = { 12_percent, 60_percent };
	// An OC_GAS cylinder in a CCR dive means bailout (effective divemode
	// OC) - mark it DILUENT so the loop stays on it.
	cyl0->cylinder_use = DILUENT;

	struct divecomputer &dc = g_dive.dcs[0];
	dc.samples.clear();
	dc.divemode = CCR;
	addSample(dc, 0, 0, 1317);
	addSample(dc, 2 * 60, feet_to_mm(100), 1317);
	addSample(dc, 60 * 60, feet_to_mm(100), 1317);
	addSample(dc, 63 * 60, feet_to_mm(20), 1317);
	addSample(dc, 100 * 60, feet_to_mm(20), 1317);
	addSample(dc, 101 * 60, 0, 1317);
	g_dive.duration.seconds = 101 * 60;
	g_dive.maxdepth.mm = feet_to_mm(100);
	lem_pdcs_result ccr = calculate_lem_pdcs(g_dive, 0);
	QVERIFY(ccr.valid);

	dc.divemode = OC;
	for (auto &s: dc.samples)
		s.setpoint.mbar = 0;
	lem_pdcs_result oc = calculate_lem_pdcs(g_dive, 0);
	QVERIFY(oc.valid);

	QVERIFY(ccr.pdcs_he8n25 != oc.pdcs_he8n25);
	QVERIFY(ccr.pdcs_he8n25 < oc.pdcs_he8n25);
	QVERIFY(ccr.pdcs_hen2_2016 < oc.pdcs_hen2_2016);
}

/* Deco monotonicity on the profile class he8n25 IS calibrated for:
 * 1.3 atm PO2 He-O2 CCR. Adding stop time must reduce risk under both
 * calibrations here. */
void TestLemScore::testHeO2DecoReducesRisk()
{
	g_dive = dive();
	g_dive.salinity = 10300;
	g_dive.surface_pressure = 1_atm;
	cylinder_t *cyl0 = g_dive.get_or_create_cylinder(0);
	cyl0->gasmix = { 12_percent, 60_percent };
	cyl0->cylinder_use = DILUENT;

	struct divecomputer &dc = g_dive.dcs[0];
	dc.divemode = CCR;
	dc.samples.clear();
	addSample(dc, 0, 0, 1317);
	addSample(dc, 2 * 60, feet_to_mm(150), 1317);
	addSample(dc, 40 * 60, feet_to_mm(150), 1317);
	addSample(dc, 45 * 60, 0, 1317);
	g_dive.duration.seconds = 45 * 60;
	g_dive.maxdepth.mm = feet_to_mm(150);
	lem_pdcs_result direct = calculate_lem_pdcs(g_dive, 0);
	QVERIFY(direct.valid);

	dc.samples.clear();
	addSample(dc, 0, 0, 1317);
	addSample(dc, 2 * 60, feet_to_mm(150), 1317);
	addSample(dc, 40 * 60, feet_to_mm(150), 1317);
	addSample(dc, 44 * 60, feet_to_mm(30), 1317);
	addSample(dc, 74 * 60, feet_to_mm(30), 1317);
	addSample(dc, 75 * 60, feet_to_mm(20), 1317);
	addSample(dc, 135 * 60, feet_to_mm(20), 1317);
	addSample(dc, 136 * 60, 0, 1317);
	g_dive.duration.seconds = 136 * 60;
	lem_pdcs_result stopped = calculate_lem_pdcs(g_dive, 0);
	QVERIFY(stopped.valid);

	QVERIFY(stopped.pdcs_he8n25 < direct.pdcs_he8n25);
	QVERIFY(stopped.pdcs_hen2_2016 < direct.pdcs_hen2_2016);
}

void TestLemScore::testRepetitiveIncreasesRisk()
{
	auto make_square = [](timestamp_t when) {
		std::unique_ptr<dive> d(new dive);
		d->when = when;
		d->salinity = 10300;
		d->surface_pressure = 1_atm;
		cylinder_t *cyl = d->get_or_create_cylinder(0);
		cyl->gasmix = { 21_percent, 0_percent };
		struct divecomputer &dc = d->dcs[0];
		dc.divemode = OC;
		addSample(dc, 0, 0);
		addSample(dc, 2 * 60, feet_to_mm(60));
		addSample(dc, 30 * 60, feet_to_mm(60));
		addSample(dc, 32 * 60, 0);
		d->duration.seconds = 32 * 60;
		d->maxdepth.mm = feet_to_mm(60);
		return d;
	};

	timestamp_t t0 = 1'700'000'000;
	auto first = make_square(t0);
	auto second = make_square(t0 + 32 * 60 + 60 * 60);

	dive_table empty;
	lem_pdcs_result iso = calculate_lem_pdcs(*second, 0, &empty);
	QVERIFY(iso.valid);
	QCOMPARE(iso.n_prior_dives, 0);

	dive_table table;
	table.put(std::move(first));
	lem_pdcs_result rep = calculate_lem_pdcs(*second, 0, &table);
	QVERIFY(rep.valid);
	QCOMPARE(rep.n_prior_dives, 1);
	QVERIFY(rep.pdcs_he8n25 > iso.pdcs_he8n25);
	QVERIFY(rep.pdcs_hen2_2016 > iso.pdcs_hen2_2016);

	auto late = make_square(t0 + 32 * 60 + 49 * 3600);
	lem_pdcs_result gap = calculate_lem_pdcs(*late, 0, &table);
	QVERIFY(gap.valid);
	QCOMPARE(gap.n_prior_dives, 0);
}

QTEST_GUILESS_MAIN(TestLemScore)
