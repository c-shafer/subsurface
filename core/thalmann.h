// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
#ifndef THALMANN_H
#define THALMANN_H

#include "gas.h"
#include "divemode.h"

struct deco_state;

/* Parameter sets available for the Thalmann EL-DCM planner mode.
 * Indices match prefs.planner_thalmann_paramset and the order of the
 * entries in thalmann.cpp. */
extern const char *thalmann_paramset_names[];
extern const int thalmann_paramset_count;

extern void thalmann_clear(struct deco_state *ds, double surface_pressure_bar);
extern void thalmann_add_segment(struct deco_state *ds, double pressure_bar, struct gasmix gasmix,
				 int period_in_seconds, int ccpo2_mbar, enum divemode_t divemode);
extern double thalmann_tolerated_ambient_pressure(struct deco_state *ds, double surface_pressure_bar);

#endif // THALMANN_H
