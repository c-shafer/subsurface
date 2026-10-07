// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
#ifndef THALMANN_H
#define THALMANN_H

#include "gas.h"
#include "divemode.h"
#include "units.h"
#include <vector>

struct deco_state;
struct dive;

/* Parameter sets available for the Thalmann EL-DCM planner mode.
 * Indices match prefs.planner_thalmann_paramset and the order of the
 * entries in thalmann.cpp. */
extern const char *thalmann_paramset_names[];
extern const int thalmann_paramset_count;

extern void thalmann_clear(struct deco_state *ds, double surface_pressure_bar);
extern void thalmann_add_segment(struct deco_state *ds, double pressure_bar, struct gasmix gasmix,
				 int period_in_seconds, int ccpo2_mbar, enum divemode_t divemode);
extern double thalmann_tolerated_ambient_pressure(struct deco_state *ds, double surface_pressure_bar);
/* Copy the dive's ATM / salinity onto deco_state so kinetics see the
 * planner GUI values. Safe to call repeatedly. */
extern void thalmann_apply_environment(struct deco_state *ds, const struct dive *dive);

/* A deco-gas switch the ascent will hit: trigger depth and what is
 * breathed from there on, matching libdeco's deco_legs. */
struct thalmann_deco_leg {
	depth_t depth;
	struct gasmix gasmix;
	int ccpo2_mbar = 0;
	enum divemode_t divemode = OC;
};

/* After tissue_at_end(), run libdeco generate_ascent_schedule() from the
 * current tensions and cache whole-minute holds for the walker. The
 * first leg is the mix already being breathed at the bottom. */
extern void thalmann_plan_stops(struct deco_state *ds, depth_t bottom,
				const std::vector<thalmann_deco_leg> &legs,
				int ascent_mm_per_sec);

/* Navy hold at `depth` so a ramp to `next_depth` is clear. Prefers the
 * generate_ascent_schedule() cache from thalmann_plan_stops(); otherwise
 * required_stop_time + Fig 29 + RNDUPD. 0 means leave now. Does not
 * advance tissues; the planner's add_segment() does that. */
extern int thalmann_hold_seconds_for_next_stop(const struct deco_state *ds, depth_t depth,
					       depth_t next_depth, struct gasmix mix, int ccpo2_mbar,
					       enum divemode_t divemode, int ascent_mm_per_sec);

/* Subtract served seconds from the cached hold at `depth`. The 12/6
 * backgas-break walker chops a stop and asks again; without this the
 * cache still returns the original full hold and the loop never ends. */
extern void thalmann_consume_hold_seconds(depth_t depth, int seconds);

/* True when the Navy hold to reach next_depth is already 0. */
extern bool thalmann_trial_ascent(const struct deco_state *ds, depth_t depth, depth_t next_depth,
				 struct gasmix mix, int ccpo2_mbar, enum divemode_t divemode,
				 int ascent_mm_per_sec);

#endif // THALMANN_H
