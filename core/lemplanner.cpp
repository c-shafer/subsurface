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
 * What LEM does answer is: "if I ascend on this schedule, what is my P_DCS?"
 * So the schedule is chosen up front by searching over whole candidate
 * ascents, and handed to the planner's own stop walker as a table of stop
 * times, through the one question the walker ever asks - trial_ascent(): may
 * I leave this depth yet? Every other part of the planner (stop grid, gas
 * switches, backgas breaks, gas consumption, the deco stop table, plan notes)
 * is reused unchanged.
 *
 * THE ALLOCATION
 *
 * Stop times are decided ALL AT ONCE, before the ascent is walked, by the
 * same global greedy libdeco's own lem_generate_schedule() uses: start from a
 * direct ascent and repeatedly add a block of time to whichever candidate
 * stop depth it most reduces P_DCS at, until the target is met. The walker
 * then just serves the times it was handed - lem_trial_ascent() below is a
 * bookkeeping check, not a model evaluation.
 *
 * It has to be global. The obvious local alternatives both fail, and it is
 * worth recording how, because both look reasonable:
 *
 * - "Ascend when surfacing from here meets the target" pays the entire
 *   obligation off at the first stop deep enough to have one, producing
 *   schedules several times longer than needed.
 * - "Ascend when the next minute is worth more one stop shallower" fixes
 *   that and breaks something worse. It is myopic and one-way: it compares
 *   marginal rates at the instant of arrival, and on a big exposure the
 *   shallower minute always wins that comparison, so the walker leaves every
 *   deep stop almost immediately, parks at the last stop - where P_DCS
 *   saturates and further time buys literally nothing - and surfaces well
 *   over target with no way back down. On 170 fsw / 100 min CCR it spent
 *   353 min to reach 4.63% against a 2.30% target that a global search meets
 *   in 262 min. Deep stop time is not interchangeable with shallow stop
 *   time, and no purely local rule can see that.
 *
 * The greedy is coarse-to-fine (block sizes halving from 32 min to 1 min,
 * backing off a block rather than overshooting the target) because at one
 * score per candidate per block the fine-grained version is too slow to run
 * on every planner keystroke.
 *
 * Termination does not depend on the target being reachable, which matters:
 * LEM risk is not always monotone in stop time. At a deep stop on a bottom
 * mix the slow compartments are still on-gassing, so waiting there *raises*
 * surfacing risk, and out of calibration (see below) it can rise with stop
 * time even shallow. When no candidate block reduces risk any further the
 * greedy stops, the plan is emitted as far as it got, and the notes report
 * the risk actually achieved against the target asked for.
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
 * - The allocation is planned against the nominal gas schedule: each candidate
 *   stop is scored on the gas the walker's gas list selects for that depth.
 *   Options that move a switch at run time (switch_at_req_stop, backgas
 *   breaks, bailout) are therefore not reflected in the allocation, so the
 *   emitted plan can score slightly better or worse than planned; the notes
 *   report the achieved figure, which is computed from the emitted plan.
 * - The ceiling drawn on the planner's profile is Buehlmann's, since LEM has
 *   no ceiling; only the stop schedule is LEM-derived. The plan notes say so.
 * - Stop allocation does not inherit tissue from a previous dive. The notes
 *   pDCS (lemscore) does: preceding dives in the 48 h / same-trip window
 *   are chained with air surface intervals.
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

/* Greedy block sizes. One minute is the granularity Navy schedules are
 * written at and the finest the allocation needs; 32 min is where it starts,
 * halving down, so a long obligation is laid down in a few dozen scores
 * instead of a few thousand. */
static const int greedy_coarse_block_seconds = 32 * 60;
static const int greedy_fine_block_seconds = 60;

/* Total stop time the allocator will lay down before giving up. Past this the
 * dive is not a decompression problem any more. */
static const int greedy_max_total_seconds = 12 * 3600;

/* A block of stop time must buy at least this much reduction in P_DCS to be
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
 * Reporting wants the parameter set's native 1 s step. The allocation search
 * does not: it scores every candidate stop for every block it lays down, a
 * few hundred scores per plan, and at 1 s each costs ~2.4 ms - seconds of
 * latency on every planner keystroke. At a 6 s step a score costs ~0.5 ms and
 * P_DCS moves by ~3e-7, five orders of magnitude below the target risks
 * anyone plans to, and the error is systematic so it very nearly cancels in
 * the differences the greedy compares. The surface tail is NOT shortened:
 * most of the risk accrues after surfacing, so truncating it would change the
 * answer. */
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
 * The global stop-time allocation
 * ---------------------------------------------------------------------- */

/* The allocation the walker is currently serving. File-static because it is a
 * per-plan decision rather than tissue state, the same way set_gf() and
 * set_vpmb_conservatism() hold the planner's other model settings - and, like
 * them, this makes plan() non-reentrant. */
namespace {
struct lem_allocation {
	std::vector<depth_t> depths;	/* deepest first */
	std::vector<int> seconds;	/* hold time allocated at each depth */
	bool valid = false;
};
}
static lem_allocation allocation;

static double depth_to_fsw(const struct dive *dive, depth_t depth, double surface_bar)
{
	return std::max((dive->depth_to_bar(depth) - surface_bar) * fsw_per_bar, 0.0);
}

/* Score one candidate allocation: the profile walked so far, continued by
 * ascending through the candidate stops holding `times` at each, then
 * surfacing. Travel to a stop is made on the gas being left; the switch
 * happens on arrival, as the walker does it. */
static double score_allocation(const struct deco_state *ds, const struct dive *dive,
			       const std::vector<lem_stop_candidate> &candidates,
			       const std::vector<int> &times, depth_t bottom_depth,
			       depth_t avg_depth, int bottom_time)
{
	std::vector<lem_node> profile = ds->lem_profile;
	double clock = ds->lem_clock_min;

	/* Travel off the bottom is on whatever the bottom leg ended on. */
	lem_breathing b;
	b.fo2 = profile.back().fo2;
	b.fhe = profile.back().fhe;
	b.po2_setpoint = profile.back().po2_setpoint;

	depth_t depth = bottom_depth;
	for (size_t i = 0; i < candidates.size(); i++) {
		if (candidates[i].depth.mm >= depth.mm)
			continue;
		append_ascent(profile, clock, dive, depth, candidates[i].depth, avg_depth,
			      bottom_time, b, ds->lem_surface_bar);
		depth = candidates[i].depth;
		b = resolve_breathing(candidates[i].gasmix, candidates[i].ccpo2_mbar,
				      candidates[i].divemode);
		if (times[i] > 0)
			append_leg(profile, clock, depth_to_fsw(dive, depth, ds->lem_surface_bar),
				   b, times[i]);
	}
	append_ascent(profile, clock, dive, depth, 0_m, avg_depth, bottom_time, b, ds->lem_surface_bar);

	return deco::lem_score(active_paramset(true), to_scorer_profile(profile)).p_dcs;
}

void lem_plan_stops(const struct deco_state *ds, const struct dive *dive,
		    const std::vector<lem_stop_candidate> &candidates, depth_t bottom_depth,
		    depth_t avg_depth, int bottom_time)
{
	allocation = lem_allocation();
	if (!ds || !dive || ds->lem_profile.empty() || candidates.empty())
		return;

	std::vector<int> times(candidates.size(), 0);
	auto score = [&](const std::vector<int> &t) {
		return score_allocation(ds, dive, candidates, t, bottom_depth, avg_depth, bottom_time);
	};

	const double target = lem_target_pdcs();
	double current = score(times);	/* direct ascent */
	int total = 0;

	for (int block = greedy_coarse_block_seconds; block >= greedy_fine_block_seconds; block /= 2) {
		while (current > target && total + block <= greedy_max_total_seconds) {
			/* Where does this block of time buy the most? */
			int best = -1;
			double best_score = current;
			for (size_t i = 0; i < candidates.size(); i++) {
				if (candidates[i].depth.mm >= bottom_depth.mm)
					continue;
				std::vector<int> trial = times;
				trial[i] += block;
				double p = score(trial);
				if (p < best_score) {
					best_score = p;
					best = static_cast<int>(i);
				}
			}
			if (best < 0 || best_score > current - marginal_gain_epsilon)
				break;	/* nothing this size buys anything - go finer */
			if (best_score <= target && block > greedy_fine_block_seconds)
				break;	/* would overshoot - lay it down finer instead */
			times[best] += block;
			total += block;
			current = best_score;
		}
	}

	allocation.valid = true;
	for (size_t i = 0; i < candidates.size(); i++) {
		allocation.depths.push_back(candidates[i].depth);
		allocation.seconds.push_back(times[i]);
	}
}

/* ------------------------------------------------------------------------
 * Serving the allocation
 * ---------------------------------------------------------------------- */

/* Time already spent at the depth the profile currently sits at. The walker
 * arrives at a stop, which puts a node at that depth, and every segment it
 * then adds there extends the tail; so the run of trailing nodes at this
 * depth is exactly the time served. */
static int seconds_at_tail_depth(const std::vector<lem_node> &profile, double depth_fsw)
{
	if (profile.empty() || !same_depth(profile.back(), depth_fsw))
		return 0;
	size_t i = profile.size() - 1;
	while (i > 0 && same_depth(profile[i - 1], depth_fsw))
		i--;
	return static_cast<int>(lrint((profile.back().time_min - profile[i].time_min) * 60.0));
}

bool lem_trial_ascent(const struct deco_state *ds, const struct dive *dive, int wait_time,
		      depth_t trial_depth)
{
	if (!allocation.valid || ds->lem_profile.empty())
		return true;

	int allocated = 0;
	for (size_t i = 0; i < allocation.depths.size(); i++) {
		if (std::abs(allocation.depths[i].mm - trial_depth.mm) <= 1) {
			allocated = allocation.seconds[i];
			break;
		}
	}
	if (allocated <= 0)
		return true;

	int served = seconds_at_tail_depth(ds->lem_profile,
					   depth_to_fsw(dive, trial_depth, ds->lem_surface_bar));
	return served + wait_time >= allocated;
}
