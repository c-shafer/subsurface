// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
#ifndef LEMPLANNER_H
#define LEMPLANNER_H

#include "units.h"
#include "gas.h"
#include "divemode.h"
#include <vector>

struct deco_state;
struct dive;

/* Parameter sets available for the LEM planner mode. Indices match
 * prefs.planner_lem_paramset and the order of the entries in lemplanner.cpp. */
extern const char *lem_paramset_names[];
extern const int lem_paramset_count;

/* One node of the profile walked so far, in the LEM model's own units
 * (minutes since the start of the dive, feet of seawater gauge). Between
 * consecutive nodes the model interpolates depth and inspired gas linearly.
 * Held in deco_state so the planner's cache/restore and CVA rewinds carry it
 * along with the tissue state; see lemplanner.cpp. */
struct lem_node {
	double time_min = 0.0;
	double depth_fsw = 0.0;
	double fo2 = 0.21;
	double fhe = 0.0;
	double po2_setpoint = 0.0; /* atm; 0 means open circuit on fo2/fhe */
};

/* The planning target: the surfacing P_DCS the generated schedule is built to
 * meet, as a probability in [0, 1]. Reads prefs.planner_lem_target_pdcs. */
extern double lem_target_pdcs();

/* Deco-engine contract, mirroring thalmann.cpp: called from clear_deco() and
 * add_segment() when the planner deco mode is LEM. */
extern void lem_clear(struct deco_state *ds, double surface_pressure_bar);
extern void lem_add_segment(struct deco_state *ds, double pressure_bar, struct gasmix gasmix,
			    int period_in_seconds, int ccpo2_mbar, enum divemode_t divemode);

/* One stop depth the ascent will pass, and what the diver will be breathing
 * there, as the planner's gas list selects it. */
struct lem_stop_candidate {
	depth_t depth;
	struct gasmix gasmix;
	int ccpo2_mbar = 0;
	enum divemode_t divemode = OC;
};

/* Choose the stop times for the ascent about to be walked and install them.
 * Call once per plan, after the bottom profile has been walked into `ds` and
 * before the ascent. `candidates` is deepest-first. See lemplanner.cpp. */
extern void lem_plan_stops(const struct deco_state *ds, const struct dive *dive,
			   const std::vector<lem_stop_candidate> &candidates,
			   depth_t bottom_depth, depth_t avg_depth, int bottom_time);

/* True when the diver has served the stop time allocated at `trial_depth`
 * (counting `wait_time` more seconds there), so the walker may ascend. */
extern bool lem_trial_ascent(const struct deco_state *ds, const struct dive *dive,
			     int wait_time, depth_t trial_depth);

#endif // LEMPLANNER_H
