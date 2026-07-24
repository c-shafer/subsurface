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
 * which deco_allowed_depth() derives the ceiling. The stop-by-stop
 * schedule walker in planner.cpp is Subsurface's own; libdeco's
 * generate_schedule() is not used. Consequences of that division of
 * labor, all deliberate:
 *
 * - Stop depths follow Subsurface's stop grid (3 m or 10 ft) and stop
 *   times follow the planner's timestep search, so schedules can differ
 *   from the published U.S. Navy tables' conventions (10 fsw grid,
 *   whole-minute holds computed with ascent-washout credit) by a
 *   stop-rounding margin even where the model itself agrees.
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
#include "pref.h"

#include "deco/eldcm.h"
#include "params/vval_79.h"
#include "params/xval_he_8_023.h"
#include "params/xval_he_8_040.h"
#include "params/xval_he_9_023.h"
#include "params/xval_he_9_040.h"

#include <array>
#include <cmath>

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
}

void thalmann_add_segment(struct deco_state *ds, double pressure_bar, struct gasmix gasmix,
			  int period_in_seconds, int ccpo2_mbar, enum divemode_t divemode)
{
	const deco::ParameterSet &base = active_paramset();
	deco::ParameterSet p = leg_params(base, gasmix, ccpo2_mbar, divemode);
	double depth_fsw = std::max((pressure_bar - ds->thalmann_surface_bar) * fsw_per_bar, 0.0);
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
