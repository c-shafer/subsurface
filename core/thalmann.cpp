// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
/* Thalmann EL-DCM (U.S. Navy exponential-linear decompression model)
 * backend for the Subsurface planner.
 *
 * This adapts the vendored libdeco implementation (core/deco-thalmann/,
 * transcribed from NEDU TR 10-09 / TR 18-05 / TR 12-01) to Subsurface's
 * deco-engine contract: clear_deco() initializes tissue state,
 * add_segment() advances it at a given ambient pressure and gas, and
 * tissue_tolerance_calc() returns the tolerated ambient pressure from
 * which deco_allowed_depth() derives the ceiling. Stop *times* for the
 * planner are taken from libdeco's generate_ascent_schedule() (same
 * walker mixed_gas_example uses) and served by planner.cpp the way LEM
 * stop times are; the Bühlmann 60 s trial-ascent search is not used.
 * Consequences of that split, all deliberate:
 *
 * - Stop depths follow the regime's 10 fsw grid and last-stop depth
 *   (20 fsw for the vendored sets). Subsurface's 3 m / 10 ft walker
 *   still emits the plan, but it serves the precomputed whole-minute
 *   holds rather than searching. Selecting the 6 m / 20 ft last-stop
 *   preference lines the walker grid up with the Navy stops.
 * - TODO: make TTIS (travel time in stops, TR 18-05 p.A-13) configurable.
 *   Reported Thalmann stop times are hold-only (TTIS=F), matching Table 14.
 *   Production appendix tables and VVal-79 Appendix D use TTIS=T (stop
 *   times include travel except the first stop). Add a planner preference
 *   and, when enabled, fold inter-stop travel into the reported stop times.
 * - The model is single-inert-gas: all inert gas in the breathing mix
 *   (N2 and He alike) is lumped into one tension tracked per
 *   compartment. The He-O2 parameter sets are calibrated for He
 *   diving, VVal-79 for air; using a set with a gas it was not
 *   calibrated for is physiologically unsupported (as it would be with
 *   the real Navy tables).
 * - The Navy "last stop" rule (SetM: no stop shallower than the
 *   regime's last stop depth, 20 fsw for all vendored sets) is
 *   implemented by clamping the ceiling: any tissue over its surfacing
 *   M-value M0 yields a ceiling of at least 20 fsw. Selecting the
 *   6 m / 20 ft last-stop planner preference reproduces the Navy
 *   convention most closely.
 * - Depth is converted to feet of seawater as gauge pressure relative
 *   to the surface pressure recorded at clear_deco() time, using the
 *   Navy's 33 fsw = 1 atm convention. Altitude and salinity
 *   corrections beyond that are not modeled (the source tables are
 *   sea-level seawater tables).
 */
#include "thalmann.h"
#include "deco.h"
#include "dive.h"
#include "pref.h"

#include "deco/eldcm.h"
#include "deco/profile.h"
#include "params/vval_79.h"
#include "params/xval_he_8_023.h"
#include "params/xval_he_8_040.h"
#include "params/xval_he_9_023.h"
#include "params/xval_he_9_040.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>

/* U.S. Navy rounded conversion: 33 fsw = 1 atm = 1.01325 bar (TR 18-05 p.A-13). */
static const double fsw_per_bar = 33.0 / 1.01325;

const char *thalmann_paramset_names[] = {
	"VVal-79 (air, TR 12-01)",
	"XVal-He-8 2.3% (He-O2 CCR, TR 18-05)",
	"XVal-He-8 4.0% (He-O2 CCR, TR 18-05)",
	"XVal-He-9 2.3% (He-O2 CCR, TR 18-05)",
	"XVal-He-9 4.0% (He-O2 CCR, TR 18-05)",
};
const int thalmann_paramset_count = 5;

/* Precomputed holds, installed by thalmann_plan_stops() and read by
 * thalmann_trial_ascent(). Same pattern as LEM's static allocation. */
static struct {
	bool valid = false;
	std::vector<int> depth_mm;
	std::vector<int> seconds;
} thalmann_alloc;

static constexpr int stop_match_mm = 200; /* ~0.65 ft */

static const deco::ParameterSet &active_paramset()
{
	static const std::array<deco::ParameterSet, 5> sets = {
		deco::params::vval_79(),
		deco::params::xval_he_8_023(),
		deco::params::xval_he_8_040(),
		deco::params::xval_he_9_023(),
		deco::params::xval_he_9_040(),
	};
	int index = prefs.planner_thalmann_paramset;
	if (index < 0 || index >= (int)sets.size())
		index = 0;
	return sets[index];
}

static size_t compartment_count(const deco::ParameterSet &p)
{
	return std::min(p.compartments.size(), (size_t)16);
}

/* Resolve the regime for the currently breathed gas: constant-PO2 for a
 * rebreather segment with a setpoint, constant-fraction otherwise. All
 * inert gas is lumped (fhe = 0, so libdeco's fn2() sees 1 - fo2) - the
 * model tracks a single inert tension. apply_gas_leg() also re-resolves
 * the per-leg SDR gate (CNDSDR_FO2), which VVal-79 needs for O2 deco. */
static deco::ParameterSet leg_params(const deco::ParameterSet &base, struct gasmix mix,
				     int ccpo2_mbar, enum divemode_t divemode)
{
	deco::DiveSegment leg;
	if (divemode == CCR && ccpo2_mbar > 0)
		leg.po2_setpoint = ccpo2_mbar / 1013.25;
	else
		leg.gas = deco::GasMix{get_o2(mix) / 1000.0, 0.0};
	return deco::apply_gas_leg(base, leg, base);
}

void thalmann_clear(struct deco_state *ds, double surface_pressure_bar)
{
	const deco::ParameterSet &p = active_paramset();
	double initial = deco::air_saturation_tension_fsw(p);
	for (int i = 0; i < 16; i++)
		ds->thalmann_tension_fsw[i] = 0.0;
	for (size_t i = 0; i < compartment_count(p); i++)
		ds->thalmann_tension_fsw[i] = initial;
	ds->thalmann_surface_bar = surface_pressure_bar;
	ds->thalmann_seg_mm = -1;
	ds->thalmann_seg_seconds = 0;
	thalmann_alloc = {};
}

void thalmann_add_segment(struct deco_state *ds, double pressure_bar, struct gasmix gasmix,
			  int period_in_seconds, int ccpo2_mbar, enum divemode_t divemode)
{
	const deco::ParameterSet &base = active_paramset();
	deco::ParameterSet p = leg_params(base, gasmix, ccpo2_mbar, divemode);
	double depth_fsw = std::max((pressure_bar - ds->thalmann_surface_bar) * fsw_per_bar, 0.0);
	int mm = (int)lrint(depth_fsw * 304.8);
	if (ds->thalmann_seg_mm < 0 || std::abs(mm - ds->thalmann_seg_mm) > stop_match_mm) {
		ds->thalmann_seg_mm = mm;
		ds->thalmann_seg_seconds = 0;
	}
	ds->thalmann_seg_seconds += period_in_seconds;

	deco::AmbientState amb = deco::resolve_ambient_state(p, depth_fsw);
	double minutes = period_in_seconds / 60.0;

	for (size_t i = 0; i < compartment_count(p); i++) {
		deco::CompartmentState cs{ds->thalmann_tension_fsw[i]};
		deco::advance_compartment(cs, amb, p.compartments[i], minutes);
		ds->thalmann_tension_fsw[i] = cs.tension_fsw;
	}
}

/* Shallowest gauge depth (fsw) at which this compartment's tension is
 * permissible. The MPTT row-offset convention (TR 10-09 "SetM") means
 * the table row at depth D constrains occupancy of depth D - DINC, and
 * the surfacing constraint M0 lives in the row at DINC. Any tension
 * over M0 requires a stop no shallower than the regime's last stop
 * depth (SetM: ascent from the last stop always targets the surface). */
static double tolerated_gauge_depth_fsw(const deco::ParameterSet &p, size_t ci, double tension_fsw)
{
	const double dinc = p.stop_depth_increment_fsw;
	const double lastop = p.last_stop_depth_fsw;
	const double m0 = deco::mptt_value(p, ci, dinc);

	if (tension_fsw <= m0)
		return 0.0;

	/* Over M0 the diver must stop at the regime's last stop depth
	 * (SetM). Reporting the ceiling as exactly that depth would make
	 * the planner's stairstep trial ascent overshoot the stop and
	 * read it as a ceiling violation, so the last stop itself would
	 * be unreachable. Report half a stop increment shallower instead:
	 * still deeper than every shallower stop level (so none can be
	 * passed while over M0), but with enough headroom that the trial
	 * ascent can land on the last stop. */
	const double must_stop_gauge = lastop - dinc / 2.0;

	if (!p.workman.empty()) {
		const deco::WorkmanCompartment &w = p.workman[ci];
		double t = (tension_fsw - w.m0_fsw) / w.slope;
		return t > lastop ? t : must_stop_gauge;
	}

	const auto &points = p.mptt.per_compartment[ci];
	double prev_t = lastop, prev_v = -1.0;
	for (const auto &pt : points) {
		double target = pt.depth_fsw - dinc;
		if (target < lastop)
			continue;
		if (pt.permissible_tension_fsw >= tension_fsw) {
			if (prev_v < 0.0)
				return must_stop_gauge;
			return prev_t + (tension_fsw - prev_v) / (pt.permissible_tension_fsw - prev_v) *
					(target - prev_t);
		}
		prev_t = target;
		prev_v = pt.permissible_tension_fsw;
	}

	/* Deeper than the table's deepest row: extrapolate with the last
	 * inter-row slope (fsw of permissible tension per fsw of depth). */
	const auto &a = points[points.size() - 2];
	const auto &b = points[points.size() - 1];
	double slope = (b.permissible_tension_fsw - a.permissible_tension_fsw) /
		       (b.depth_fsw - a.depth_fsw);
	return prev_t + (tension_fsw - prev_v) / slope;
}

double thalmann_tolerated_ambient_pressure(struct deco_state *ds, double surface_pressure_bar)
{
	const deco::ParameterSet &p = active_paramset();
	double max_gauge_fsw = 0.0;

	for (size_t i = 0; i < 16; i++) {
		if (i < compartment_count(p)) {
			double gauge = tolerated_gauge_depth_fsw(p, i, ds->thalmann_tension_fsw[i]);
			ds->tolerated_by_tissue[i] = surface_pressure_bar + gauge / fsw_per_bar;
			if (gauge >= max_gauge_fsw) {
				max_gauge_fsw = gauge;
				ds->ci_pointing_to_guiding_tissue = (int)i;
			}
		} else {
			ds->tolerated_by_tissue[i] = 0.0;
		}
	}
	return surface_pressure_bar + max_gauge_fsw / fsw_per_bar;
}

void thalmann_plan_stops(struct deco_state *ds, const struct dive *dive,
			 depth_t bottom_depth, int ascent_mm_per_sec,
			 struct gasmix bottom_mix, int bottom_ccpo2, enum divemode_t bottom_mode,
			 const std::vector<thalmann_deco_leg> &switches)
{
	thalmann_alloc = {};
	if (!ds || !dive)
		return;

	deco::ParameterSet p = active_paramset();
	if (bottom_mode == CCR && bottom_ccpo2 > 0) {
		p.constant_po2 = true;
		p.po2_setpoint_atm = bottom_ccpo2 / 1013.25;
	} else {
		p.constant_po2 = false;
		p.fn2 = 1.0 - get_o2(bottom_mix) / 1000.0;
	}

	deco::Profile legs;
	for (const auto &sw : switches) {
		deco::DiveSegment seg;
		seg.depth_fsw = sw.depth.mm / 304.8;
		if (sw.divemode == CCR && sw.ccpo2_mbar > 0)
			seg.po2_setpoint = sw.ccpo2_mbar / 1013.25;
		else
			seg.gas = deco::GasMix{get_o2(sw.gasmix) / 1000.0, 0.0};
		legs.push_back(seg);
	}

	const size_t n = compartment_count(p);
	std::vector<deco::CompartmentState> state(n);
	for (size_t i = 0; i < n; i++)
		state[i].tension_fsw = ds->thalmann_tension_fsw[i];

	// Nominal feet as fsw so 100 ft is 100 fsw on the Navy stop grid,
	// not 100.3 fsw from 1.030 kg/L salinity.
	double depth_fsw = bottom_depth.mm / 304.8;
	double rate = ascent_mm_per_sec > 0 ? ascent_mm_per_sec * 60.0 / 304.8 : 30.0;

	deco::Schedule s = deco::generate_ascent_schedule(p, std::move(state), depth_fsw, rate, legs);

	thalmann_alloc.valid = true;
	for (const auto &st : s.stops) {
		thalmann_alloc.depth_mm.push_back((int)feet_to_mm(st.depth_fsw));
		thalmann_alloc.seconds.push_back((int)lrint(st.hold_time_min * 60.0));
	}
}

bool thalmann_trial_ascent(const struct deco_state *ds, int wait_time, depth_t trial_depth)
{
	if (!thalmann_alloc.valid)
		return true;

	int allocated = 0;
	for (size_t i = 0; i < thalmann_alloc.depth_mm.size(); i++) {
		if (std::abs(thalmann_alloc.depth_mm[i] - trial_depth.mm) <= stop_match_mm) {
			allocated = thalmann_alloc.seconds[i];
			break;
		}
	}
	if (allocated <= 0)
		return true;

	int served = 0;
	if (ds->thalmann_seg_mm >= 0 && std::abs(ds->thalmann_seg_mm - trial_depth.mm) <= stop_match_mm)
		served = ds->thalmann_seg_seconds;
	return served + wait_time >= allocated;
}
