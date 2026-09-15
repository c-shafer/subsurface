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

/* Choose stop times with libdeco's generate_ascent_schedule() from the
 * tissues already loaded through the bottom, then install them for the
 * walker. Call once per plan, after tissue_at_end, before the ascent. */
extern void thalmann_plan_stops(struct deco_state *ds, const struct dive *dive,
				depth_t bottom_depth, int ascent_mm_per_sec,
				struct gasmix bottom_mix, int bottom_ccpo2, enum divemode_t bottom_mode,
				const std::vector<thalmann_deco_leg> &switches);

/* True when the diver has served the precomputed hold at trial_depth
 * (counting wait_time more seconds there), so the walker may ascend. */
extern bool thalmann_trial_ascent(const struct deco_state *ds, int wait_time, depth_t trial_depth);

#endif // THALMANN_H
