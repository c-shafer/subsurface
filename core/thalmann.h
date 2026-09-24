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

/* A deco-gas switch the ascent will hit: trigger depth and what is
 * breathed from there on, matching libdeco's deco_legs. */
struct thalmann_deco_leg {
	depth_t depth;
	struct gasmix gasmix;
	int ccpo2_mbar = 0;
	enum divemode_t divemode = OC;
};

/* Navy hold at `depth` so a ramp to `next_depth` is clear: required_stop_time
 * + Fig 29 optimize_stop_time + RNDUPD. 0 means leave now. Does not advance
 * tissues; the planner's add_segment() does that. */
extern int thalmann_hold_seconds_for_next_stop(const struct deco_state *ds, depth_t depth,
					       depth_t next_depth, struct gasmix mix, int ccpo2_mbar,
					       enum divemode_t divemode, int ascent_mm_per_sec);

/* True when the Navy hold to reach next_depth is already 0. */
extern bool thalmann_trial_ascent(const struct deco_state *ds, depth_t depth, depth_t next_depth,
				 struct gasmix mix, int ccpo2_mbar, enum divemode_t divemode,
				 int ascent_mm_per_sec);

#endif // THALMANN_H
