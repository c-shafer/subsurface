// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
/* LEM (probabilistic Linear-Exponential Multigas) backend for the
 * Subsurface planner, using the vendored libdeco scorer in
 * core/deco-thalmann/deco/lem.h (Gerth & Johnson, NEDU TR 02-10).
 *
 * WHY THIS DOES NOT LOOK LIKE thalmann.cpp
 *
 * Buehlmann, VPM-B and Thalmann are deterministic: each defines, from the
 * current tissue state alone, a tolerated ambient pressure, and the planner
 * turns that ceiling into stops. LEM defines no such thing. It is a risk
 * model: it maps a *whole profile*, including everything that happens after
 * surfacing, to a single probability of decompression sickness. There is no
 * "shallowest permissible depth right now" to report, so LEM cannot be
 * plugged in at tissue_tolerance_calc() the way the other three are.
 *
 * What LEM does answer is: "if I go up from here, what is my P_DCS?" That is
 * enough to drive the planner's existing stop walker, because the walker only
 * ever asks one question - trial_ascent(): may I leave this depth yet? So
 * this backend supplies a probabilistic trial_ascent() instead of a ceiling,
 * and every other part of the planner (stop grid, gas switches, backgas
 * breaks, gas consumption, the deco stop table, plan notes) is reused
 * unchanged.
 *
 * THE CRITERION
 *
 * The diver is clear to ascend when either
 *
 *   (a) surfacing from here already meets the target risk, or
 *   (b) the next minute of decompression is worth more at the next stop
 *       level than it is here.
 *
 * (a) is the planning goal: prefs.planner_lem_target_pdcs, the P_DCS the
 * schedule is built to.
 *
 * (b) is what allocates the stop time, and it has to be a comparison rather
 * than a threshold. Asking only "does another minute here still lower my
 * risk?" is not enough: it does, almost everywhere, so the walker would pay
 * off the entire obligation at the first stop deep enough to have one. That
 * produces schedules that meet the target with several times more total stop
 * time than needed - the deep-stop pathology. Both branches of (b) spend the
 * same minute, so scoring them against each other asks the question that
 * actually matters, which depth that minute is worth more at, and time
 * migrates shallow on its own until the last stop, where the shallower
 * alternative is the surface and the comparison reduces to "does staying
 * still pay?".
 *
 * That is the same allocation libdeco's own lem_generate_schedule() reaches
 * by global greedy search, expressed locally so the planner's own walker can
 * drive it. It also terminates unconditionally, which matters more than it
 * sounds: LEM risk is not always monotone in stop time. At a deep stop on a
 * bottom mix the slow compartments are still on-gassing, so waiting there
 * *raises* surfacing risk, and out-of-calibration (see below) it can rise
 * with stop time even shallow. A ceiling-style "wait for it to clear" test
 * would hang; (b) always eventually prefers the shallower option. If the
 * target is simply unreachable, the walker surfaces rather than hanging, and
 * the plan notes report the risk actually achieved.
 *
 * SCOPE AND CAVEATS, all deliberate:
 *
 * - Both parameter sets are calibrated for 1.3 ata PO2 closed-circuit He-O2
 *   (the MK 16 MOD 1 regime TR 02-10 was written for). That is not a footnote
 *   here the way it is for a deterministic model: out of calibration the model
 *   does not merely give conservative or aggressive stops, it stops behaving
 *   like a decompression model at all. On open-circuit air, LEM risk barely
 *   responds to stop time and is not even monotone in it - a 5 min stop at
 *   20 fsw helps, 40 min is worse than none - so there is no schedule to find
 *   and the planner will return a short one that does not meet target. Use
 *   these sets with the gas they were fitted for, as with the printed Navy
 *   tables.
 * - The trial continuation breathes one gas all the way up, the one passed to
 *   trial_ascent(), exactly as the deterministic trial ascent does. Deco gas
 *   switches shallower than the trial depth are therefore not credited when
 *   deciding to leave a stop, which is conservative: the real ascent switches
 *   to a richer mix and scores lower than the trial predicted.
 * - The ceiling drawn on the planner's profile is Buehlmann's, since LEM has
 *   no ceiling; only the stop schedule is LEM-derived. The plan notes say so.
 * - Tissue loading inherited from a previous dive (init_decompression) is not
 *   represented: the LEM profile starts at surface equilibrium at the start of
 *   the planned dive, the same isolation lemscore.cpp documents for scoring.
 * - Layer A only (dissolved-gas hazard core), as vendored; see lem.h.
 */
#include "lemplanner.h"
#include "deco.h"
#include "dive.h"
#include "planner.h"
#include "pref.h"

#include "deco/lem.h"
#include "params/lem_he8n25.h"
#include "params/lem_hen2_2016.h"

#include <array>
#include <cmath>

/* U.S. Navy rounded conversion: 33 fsw = 1 atm = 1.01325 bar, as in
 * thalmann.cpp and lemscore.cpp. */
static const double fsw_per_bar = 33.0 / 1.01325;

/* Ascent granularity of the trial continuation. The planner walks its own
 * trial ascents in 2 s steps (base_timestep in planner.cpp); matching that
 * keeps the profile the risk is computed from identical in shape to the
 * profile the walker will actually emit. */
static const int trial_timestep = 2;

/* Probe used to answer "does waiting longer still help?". One minute is the
 * granularity Navy schedules are written at, and the granularity the planner's
 * stop search converges to. */
static const int marginal_probe_seconds = 60;

/* A minute of stop time must buy at least this much reduction in P_DCS to be
 * worth spending. Set well above the scorer's own quadrature noise at the
 * search step below (which is a few times 1e-7), and far below any risk
 * difference a diver would care about. */
static const double marginal_gain_epsilon = 1e-6;

const char *lem_paramset_names[] = {
	"LEM-He8N25 (TR 02-10 Table 4)",
	"LEM-HeN2-2016 (2016 He/N2 re-optimization)",
};
const int lem_paramset_count = 2;

/* The scorer is used two ways, and they want different quadrature steps.
 *
 * Reporting wants the parameter set's native 1 s step. Searching does not:
 * the walker makes a few hundred trial ascents per plan, and at 1 s each
 * score costs ~2.4 ms, which would put seconds of latency into every planner
 * keystroke. At a 6 s step a score costs ~0.5 ms and P_DCS moves by ~3e-7 -
 * five orders of magnitude below the target risks anyone plans to, and the
 * error is systematic, so it very nearly cancels in the differences the
 * criterion actually compares. The surface tail is NOT shortened: most of the
 * risk accrues after surfacing, so truncating it would change the answer. */
static const double search_dt_min = 6.0 / 60.0;

static const deco::LemParams &active_paramset(bool for_search)
{
	static const auto build = [](bool search) {
		std::array<deco::LemParams, 2> sets = {
			deco::params::lem_he8n25(),
			deco::params::lem_hen2_2016(),
		};
		if (search) {
			for (auto &p: sets)
				p.dt_min = search_dt_min;
		}
		return sets;
	};
	static const std::array<deco::LemParams, 2> report_sets = build(false);
	static const std::array<deco::LemParams, 2> search_sets = build(true);

	const auto &sets = for_search ? search_sets : report_sets;
	int index = prefs.planner_lem_paramset;
	if (index < 0 || index >= (int)sets.size())
		index = 0;
	return sets[index];
}

double lem_target_pdcs()
{
	/* Stored in hundredths of a percent so the preference can stay an int:
	 * 230 means 2.30% means a probability of 0.023. */
	double target = prefs.planner_lem_target_pdcs / 10000.0;
	if (target <= 0.0)
		return 0.0001;
	if (target >= 1.0)
		return 0.9999;
	return target;
}

/* ------------------------------------------------------------------------
 * Profile assembly
 * ---------------------------------------------------------------------- */

/* What the diver is breathing, in the scorer's terms. */
struct lem_breathing {
	double fo2 = 0.21;
	double fhe = 0.0;
	double po2_setpoint = 0.0;
};

static lem_breathing resolve_breathing(struct gasmix gasmix, int ccpo2_mbar, enum divemode_t divemode)
{
	lem_breathing b;
	b.fo2 = get_o2(gasmix) / 1000.0;
	b.fhe = get_he(gasmix) / 1000.0;
	if (divemode == CCR && ccpo2_mbar > 0) {
		b.po2_setpoint = ccpo2_mbar / 1013.25;
		/* He fraction of the non-O2 diluent balance; the rest is N2.
		 * Same reduction as lemscore.cpp. */
		double inert = 1.0 - b.fo2;
		b.fhe = inert > 0.0 ? b.fhe / inert : 0.0;
	}
	return b;
}

static bool same_breathing(const lem_node &n, const lem_breathing &b)
{
	return std::fabs(n.fo2 - b.fo2) < 1e-9 &&
	       std::fabs(n.fhe - b.fhe) < 1e-9 &&
	       std::fabs(n.po2_setpoint - b.po2_setpoint) < 1e-9;
}

static bool same_depth(const lem_node &n, double depth_fsw)
{
	return std::fabs(n.depth_fsw - depth_fsw) < 0.01;
}

static lem_node make_node(double time_min, double depth_fsw, const lem_breathing &b)
{
	lem_node n;
	n.time_min = time_min;
	n.depth_fsw = depth_fsw;
	n.fo2 = b.fo2;
	n.fhe = b.fhe;
	n.po2_setpoint = b.po2_setpoint;
	return n;
}

/* Append one constant-depth, constant-gas leg of `period_in_seconds` at
 * `depth_fsw`, advancing `clock_min`.
 *
 * The result is a node list, not a sample list: a leg needs a node at each
 * end, consecutive holds at one depth collapse into a single extended node,
 * and a gas change gets its own node at the switching instant so the scorer's
 * linear interpolation does not smear the new mix backwards over the previous
 * leg. Ramps (the planner descends and ascends in 1-2 s constant-depth steps)
 * are left to interpolate between leg-end nodes, which is both cheaper and
 * closer to the real profile than a staircase. */
static void append_leg(std::vector<lem_node> &profile, double &clock_min, double depth_fsw,
		       const lem_breathing &b, int period_in_seconds)
{
	double t0 = clock_min;
	double t1 = t0 + period_in_seconds / 60.0;

	if (profile.empty()) {
		profile.push_back(make_node(t0, depth_fsw, b));
	} else {
		const lem_node &back = profile.back();
		bool gas_continues = same_breathing(back, b);
		if (gas_continues && same_depth(back, depth_fsw) && profile.size() >= 2 &&
		    same_breathing(profile[profile.size() - 2], b) &&
		    same_depth(profile[profile.size() - 2], depth_fsw)) {
			/* Still sitting at the same depth on the same gas. */
			profile.back().time_min = t1;
			clock_min = t1;
			return;
		}
		if (!gas_continues)
			profile.push_back(make_node(t0, back.depth_fsw, b));
	}
	profile.push_back(make_node(t1, depth_fsw, b));
	clock_min = t1;
}

/* Append an ascent from `from_fsw` to `to_fsw`, following the planner's own
 * depth-dependent ascent rates (ascent_velocity()). */
static void append_ascent(std::vector<lem_node> &profile, double &clock_min, const struct dive *dive,
			  depth_t from_depth, depth_t to_depth, depth_t avg_depth, int bottom_time,
			  const lem_breathing &b, double surface_bar)
{
	depth_t depth = from_depth;
	while (depth.mm > to_depth.mm) {
		int deltad = ascent_velocity(depth, avg_depth, bottom_time) * trial_timestep;
		if (depth.mm - deltad < to_depth.mm)
			deltad = depth.mm - to_depth.mm;
		depth.mm -= deltad;
		double depth_fsw = std::max((dive->depth_to_bar(depth) - surface_bar) * fsw_per_bar, 0.0);
		append_leg(profile, clock_min, depth_fsw, b, trial_timestep);
	}
}

static deco::LemProfile to_scorer_profile(const std::vector<lem_node> &profile)
{
	deco::LemProfile out;
	out.reserve(profile.size());
	for (const lem_node &n: profile) {
		deco::LemNode node;
		node.time_min = n.time_min;
		node.depth_fsw = n.depth_fsw;
		if (n.po2_setpoint > 0.0) {
			node.po2_setpoint = n.po2_setpoint;
			node.diluent_fhe = n.fhe;
		} else {
			node.gas = deco::GasMix{n.fo2, n.fhe};
		}
		out.push_back(node);
	}
	return out;
}

/* ------------------------------------------------------------------------
 * Deco-engine contract
 * ---------------------------------------------------------------------- */

void lem_clear(struct deco_state *ds, double surface_pressure_bar)
{
	ds->lem_profile.clear();
	ds->lem_clock_min = 0.0;
	ds->lem_surface_bar = surface_pressure_bar;
}

void lem_add_segment(struct deco_state *ds, double pressure_bar, struct gasmix gasmix,
		     int period_in_seconds, int ccpo2_mbar, enum divemode_t divemode)
{
	double depth_fsw = std::max((pressure_bar - ds->lem_surface_bar) * fsw_per_bar, 0.0);
	append_leg(ds->lem_profile, ds->lem_clock_min, depth_fsw,
		   resolve_breathing(gasmix, ccpo2_mbar, divemode), period_in_seconds);
}

/* ------------------------------------------------------------------------
 * The probabilistic trial ascent
 * ---------------------------------------------------------------------- */

/* Score the profile so far plus a hypothetical continuation: wait
 * `wait_time` seconds at `trial_depth`, ascend to `hold_depth` and wait
 * `hold_time` seconds there, then ascend to the surface. Passing
 * hold_time = 0 gives the plain "leave now and surface" continuation.
 * `for_search` selects the coarse quadrature step. */
static double pdcs_after_ascent(const struct deco_state *ds, const struct dive *dive, int wait_time,
				depth_t trial_depth, depth_t hold_depth, int hold_time,
				depth_t avg_depth, int bottom_time, struct gasmix gasmix,
				int ccpo2_mbar, enum divemode_t divemode, bool for_search)
{
	if (ds->lem_profile.empty())
		return 0.0;

	std::vector<lem_node> profile = ds->lem_profile;
	double clock = ds->lem_clock_min;
	lem_breathing b = resolve_breathing(gasmix, ccpo2_mbar, divemode);
	double trial_fsw = std::max((dive->depth_to_bar(trial_depth) - ds->lem_surface_bar) * fsw_per_bar, 0.0);

	if (wait_time > 0)
		append_leg(profile, clock, trial_fsw, b, wait_time);
	if (hold_time > 0 && hold_depth.mm < trial_depth.mm) {
		append_ascent(profile, clock, dive, trial_depth, hold_depth, avg_depth, bottom_time,
			      b, ds->lem_surface_bar);
		double hold_fsw = std::max((dive->depth_to_bar(hold_depth) - ds->lem_surface_bar) * fsw_per_bar, 0.0);
		append_leg(profile, clock, hold_fsw, b, hold_time);
		append_ascent(profile, clock, dive, hold_depth, 0_m, avg_depth, bottom_time,
			      b, ds->lem_surface_bar);
	} else {
		append_ascent(profile, clock, dive, trial_depth, 0_m, avg_depth, bottom_time,
			      b, ds->lem_surface_bar);
	}

	return deco::lem_score(active_paramset(for_search), to_scorer_profile(profile)).p_dcs;
}

double lem_pdcs_after_ascent(const struct deco_state *ds, const struct dive *dive, int wait_time,
			     depth_t trial_depth, depth_t avg_depth, int bottom_time,
			     struct gasmix gasmix, int ccpo2_mbar, enum divemode_t divemode)
{
	return pdcs_after_ascent(ds, dive, wait_time, trial_depth, 0_m, 0, avg_depth, bottom_time,
				 gasmix, ccpo2_mbar, divemode, false);
}

bool lem_trial_ascent(const struct deco_state *ds, const struct dive *dive, int wait_time,
		      depth_t trial_depth, depth_t stoplevel, depth_t avg_depth, int bottom_time,
		      struct gasmix gasmix, int ccpo2_mbar, enum divemode_t divemode)
{
	if (ds->lem_profile.empty())
		return true;

	double now = pdcs_after_ascent(ds, dive, wait_time, trial_depth, 0_m, 0, avg_depth,
				       bottom_time, gasmix, ccpo2_mbar, divemode, true);
	if (now <= lem_target_pdcs())
		return true; /* (a) the target is met - go up */

	/* (b) Spend the next minute wherever it is worth more. Both branches
	 * cost the same minute; the only difference is the depth it is spent
	 * at, so this is a straight comparison of what that minute buys. */
	double stay = pdcs_after_ascent(ds, dive, wait_time + marginal_probe_seconds, trial_depth,
					0_m, 0, avg_depth, bottom_time, gasmix, ccpo2_mbar,
					divemode, true);

	/* At the last stop the shallower alternative is the surface, where a
	 * "stop" is not a thing: the dive is over. Scoring one as if it were
	 * would also charge the model a minute of the breathing gas at 1 atm,
	 * which for a rebreather is a minute of near-pure O2 and beats any
	 * real stop on inert-gas washout alone. So the alternative is simply
	 * surfacing, and (b) reduces to "does staying still pay?". */
	double move = stoplevel.mm > 0
			      ? pdcs_after_ascent(ds, dive, wait_time, trial_depth, stoplevel,
						  marginal_probe_seconds, avg_depth, bottom_time,
						  gasmix, ccpo2_mbar, divemode, true)
			      : now;

	return move <= stay + marginal_gain_epsilon;
}
