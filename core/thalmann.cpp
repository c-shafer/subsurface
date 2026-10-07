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
 * which deco_allowed_depth() derives the ceiling. Stop *times* are
 * computed live at each stoplevel from current EL-DCM tensions
 * (required_stop_time + Fig 29 + RNDUPD), the TR 10-09 / NDC shape,
 * not a one-shot table played back by wait_until().
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
 * - Planned depths are fsw as mm/304.8 scaled by salinity/10270 when
 *   deco_state::thalmann_depth_mm is set. 10270 g/10 L is 1.027 kg/L,
 *   the density at which 33 fsw = 1 atm. Fresh or denser water therefore
 *   changes loading; Custom 1.027 kg/L is identity with the Navy tables.
 *   ATM / altitude map onto ParameterSet::surface_pressure_fsw
 *   (1013 mbar → 33 fsw). Without thalmann_depth_mm (logged-dive replay),
 *   gauge pressure is converted at 33 fsw = 1 atm.
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
/* Subsurface salinity unit: g/10 L. USN_SALINITY 1.027 kg/L is 33 fsw/atm. */

const char *thalmann_paramset_names[] = {
	"VVal-79 (air, TR 12-01)",
	"XVal-He-8 2.3% (He-O2 CCR, TR 18-05)",
	"XVal-He-8 4.0% (He-O2 CCR, TR 18-05)",
	"XVal-He-9 2.3% (He-O2 CCR, TR 18-05)",
	"XVal-He-9 4.0% (He-O2 CCR, TR 18-05)",
};
const int thalmann_paramset_count = 5;

static constexpr int stop_match_mm = 200; /* ~0.65 ft */

/* Fig 13 cycling clock, if a regime has o2_cycling_enabled. Reset in
 * thalmann_clear(). Subsurface param sets leave cycling off (gas-list
 * switches instead); this still keeps AIR/O2 cycling plans correct. */
static deco::O2AirCyclingState thalmann_cyc;

/* Whole-ascent holds from generate_ascent_schedule(). File-static the
 * same way LEM's allocation is: plan() is not reentrant. */
namespace {
struct thalmann_allocation {
	std::vector<int> depth_ft;
	std::vector<int> seconds;
	bool valid = false;
};
}
static thalmann_allocation thalmann_alloc;

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

static int effective_salinity(int salinity)
{
	if (!salinity)
		return SEAWATER_SALINITY;
	if (salinity < 500)
		return salinity + FRESHWATER_SALINITY;
	return salinity;
}

/* Geometric mm as Navy fsw, scaled so 1.027 kg/L is identity. */
static double kinetic_fsw_from_mm(int mm, int salinity)
{
	return std::max(mm / 304.8 * (effective_salinity(salinity) / (double)USN_SALINITY), 0.0);
}

static double kinetic_fsw(const struct deco_state *ds, int mm)
{
	return kinetic_fsw_from_mm(mm, ds ? ds->thalmann_salinity : 0);
}

/* GUI sea-level ATM is 1_atm = 1013 mbar. Map that onto Navy 33 fsw. */
static double surface_fsw_from_bar(double surface_bar)
{
	if (!(surface_bar > 0.0))
		return 33.0;
	return 33.0 * surface_bar * 1000.0 / (1_atm).mbar;
}

static deco::ParameterSet params_for(const struct deco_state *ds)
{
	deco::ParameterSet p = active_paramset();
	if (ds)
		p.surface_pressure_fsw = surface_fsw_from_bar(ds->thalmann_surface_bar);
	return p;
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
	ds->thalmann_surface_bar = surface_pressure_bar;
	ds->thalmann_salinity = 0;
	deco::ParameterSet p = params_for(ds);
	double initial = deco::air_saturation_tension_fsw(p);
	for (int i = 0; i < 16; i++)
		ds->thalmann_tension_fsw[i] = 0.0;
	for (size_t i = 0; i < compartment_count(p); i++)
		ds->thalmann_tension_fsw[i] = initial;
	ds->thalmann_seg_mm = -1;
	ds->thalmann_seg_seconds = 0;
	ds->thalmann_depth_mm = -1;
	thalmann_cyc = {};
	thalmann_alloc = {};
}

void thalmann_apply_environment(struct deco_state *ds, const struct dive *dive)
{
	if (!ds || !dive)
		return;
	int sal = 0;
	if (!dive->dcs.empty() && dive->dcs[0].salinity)
		sal = dive->dcs[0].salinity;
	if (!sal)
		sal = dive->get_salinity();
	ds->thalmann_salinity = sal;
	pressure_t sp = dive->get_surface_pressure();
	if (sp.mbar > 0)
		ds->thalmann_surface_bar = sp.mbar / 1000.0;
}

void thalmann_add_segment(struct deco_state *ds, double pressure_bar, struct gasmix gasmix,
			  int period_in_seconds, int ccpo2_mbar, enum divemode_t divemode)
{
	deco::ParameterSet base = params_for(ds);
	deco::ParameterSet p = leg_params(base, gasmix, ccpo2_mbar, divemode);
	double depth_fsw;
	if (ds->thalmann_depth_mm >= 0)
		depth_fsw = kinetic_fsw(ds, ds->thalmann_depth_mm);
	else
		depth_fsw = std::max((pressure_bar - ds->thalmann_surface_bar) * fsw_per_bar, 0.0);
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

	/* Over M0 the diver must stop at the regime's last stop (SetM).
	 * Occupying that stop is decided by hold seconds, not by comparing
	 * this ceiling to the walker depth. */
	if (!p.workman.empty()) {
		const deco::WorkmanCompartment &w = p.workman[ci];
		double t = (tension_fsw - w.m0_fsw) / w.slope;
		return t > lastop ? t : lastop;
	}

	const auto &points = p.mptt.per_compartment[ci];
	double prev_t = lastop, prev_v = -1.0;
	for (const auto &pt : points) {
		double target = pt.depth_fsw - dinc;
		if (target < lastop)
			continue;
		if (pt.permissible_tension_fsw >= tension_fsw) {
			if (prev_v < 0.0)
				return lastop;
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

void thalmann_plan_stops(struct deco_state *ds, depth_t bottom,
			 const std::vector<thalmann_deco_leg> &legs,
			 int ascent_mm_per_sec)
{
	thalmann_alloc = {};
	if (!ds || bottom.mm <= 0)
		return;

	deco::ParameterSet base = params_for(ds);
	const size_t n = compartment_count(base);
	std::vector<deco::CompartmentState> state(n);
	for (size_t i = 0; i < n; i++)
		state[i].tension_fsw = ds->thalmann_tension_fsw[i];

	deco::Profile deco_legs;
	for (const auto &leg: legs) {
		deco::DiveSegment seg;
		seg.depth_fsw = kinetic_fsw(ds, leg.depth.mm);
		if (leg.divemode == CCR && leg.ccpo2_mbar > 0)
			seg.po2_setpoint = leg.ccpo2_mbar / 1013.25;
		else
			seg.gas = deco::GasMix{get_o2(leg.gasmix) / 1000.0, 0.0};
		deco_legs.push_back(seg);
	}

	double scale = effective_salinity(ds->thalmann_salinity) / (double)USN_SALINITY;
	double rate = ascent_mm_per_sec > 0 ? ascent_mm_per_sec * 60.0 / 304.8 * scale : 30.0;
	double cur = kinetic_fsw(ds, bottom.mm);
	deco::Schedule sched = deco::generate_ascent_schedule(base, std::move(state), cur, rate, deco_legs);
	for (const auto &st: sched.stops) {
		if (st.hold_time_min <= 0.0)
			continue;
		thalmann_alloc.depth_ft.push_back((int)std::lround(st.depth_fsw));
		int seconds = (int)std::lrint(st.hold_time_min * 60.0);
		if (seconds > 48 * 3600)
			seconds = 48 * 3600;
		thalmann_alloc.seconds.push_back(seconds);
	}
	thalmann_alloc.valid = true;
}

int thalmann_hold_seconds_for_next_stop(const struct deco_state *ds, depth_t depth,
					depth_t next_depth, struct gasmix mix, int ccpo2_mbar,
					enum divemode_t divemode, int ascent_mm_per_sec)
{
	if (!ds || depth.mm <= next_depth.mm)
		return 0;

	if (thalmann_alloc.valid) {
		int ft = (int)std::lround(depth.mm / 304.8);
		for (size_t i = 0; i < thalmann_alloc.depth_ft.size(); i++) {
			if (thalmann_alloc.depth_ft[i] == ft)
				return thalmann_alloc.seconds[i];
		}
		return 0;
	}

	deco::ParameterSet p = leg_params(params_for(ds), mix, ccpo2_mbar, divemode);
	const size_t n = compartment_count(p);
	double current_fsw = kinetic_fsw(ds, depth.mm);
	double next_fsw = next_depth.mm <= 0 ? 0.0 : kinetic_fsw(ds, next_depth.mm);
	double scale = effective_salinity(ds->thalmann_salinity) / (double)USN_SALINITY;
	double rate = ascent_mm_per_sec > 0 ? ascent_mm_per_sec * 60.0 / 304.8 * scale : 30.0;

	std::vector<deco::CompartmentState> state(n);
	for (size_t i = 0; i < n; i++)
		state[i].tension_fsw = ds->thalmann_tension_fsw[i];

	double hold_min = 0.0;
	if (p.o2_cycling_enabled && current_fsw <= p.o2ceil_fsw + 1e-9) {
		deco::O2AirCyclingState cyc = thalmann_cyc;
		deco::RoundedStopTime rnd = deco::run_o2_air_cycling_hold(
			p, state, cyc, current_fsw, next_fsw, rate, params_for(ds));
		hold_min = rnd.occupancy_min;
		/* Tissues are not committed here; planner add_segment() advances
		 * them. Cycling clock advances when those seconds are actually
		 * served at this depth on this gas. */
	} else {
		deco::AmbientState amb = deco::resolve_ambient_state(p, current_fsw);
		std::vector<double> tensions(n), targets(n);
		for (size_t i = 0; i < n; i++) {
			tensions[i] = state[i].tension_fsw;
			targets[i] = deco::mptt_value(p, i, current_fsw);
		}
		hold_min = deco::required_stop_time_all(p, tensions, amb, targets);
		if (!std::isfinite(hold_min) || hold_min <= 0.0)
			return 0;
		hold_min = deco::optimize_stop_time(p, state, current_fsw, next_fsw, targets,
						    hold_min, rate);
		if (p.round_stop_time_up && hold_min > 0.0)
			hold_min = std::ceil(hold_min - 1e-9);
	}
	if (!std::isfinite(hold_min) || hold_min <= 0.0)
		return 0;
	int seconds = (int)lrint(hold_min * 60.0);
	if (seconds > 48 * 3600)
		seconds = 48 * 3600;
	return seconds;
}

void thalmann_consume_hold_seconds(depth_t depth, int seconds)
{
	if (!thalmann_alloc.valid || seconds <= 0)
		return;
	int ft = (int)std::lround(depth.mm / 304.8);
	for (size_t i = 0; i < thalmann_alloc.depth_ft.size(); i++) {
		if (thalmann_alloc.depth_ft[i] == ft) {
			if (thalmann_alloc.seconds[i] > seconds)
				thalmann_alloc.seconds[i] -= seconds;
			else
				thalmann_alloc.seconds[i] = 0;
			return;
		}
	}
}

bool thalmann_trial_ascent(const struct deco_state *ds, depth_t depth, depth_t next_depth,
			  struct gasmix mix, int ccpo2_mbar, enum divemode_t divemode,
			  int ascent_mm_per_sec)
{
	return thalmann_hold_seconds_for_next_stop(ds, depth, next_depth, mix, ccpo2_mbar,
						   divemode, ascent_mm_per_sec) == 0;
}
