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

/* P_DCS of the profile walked so far, continued by waiting `wait_time`
 * seconds at `trial_depth` and then ascending straight to the surface on
 * `gasmix`. This is the quantity the planner's stop walker is driven by. */
extern double lem_pdcs_after_ascent(const struct deco_state *ds, const struct dive *dive,
				    int wait_time, depth_t trial_depth, depth_t avg_depth,
				    int bottom_time, struct gasmix gasmix, int ccpo2_mbar,
				    enum divemode_t divemode);

/* The LEM analogue of a ceiling test: true when the diver may leave
 * `trial_depth` for `stoplevel`. See lemplanner.cpp for the criterion. */
extern bool lem_trial_ascent(const struct deco_state *ds, const struct dive *dive,
			     int wait_time, depth_t trial_depth, depth_t stoplevel,
			     depth_t avg_depth, int bottom_time, struct gasmix gasmix,
			     int ccpo2_mbar, enum divemode_t divemode);

#endif // LEMPLANNER_H
