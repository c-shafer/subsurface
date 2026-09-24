// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
#ifndef BVM3_H
#define BVM3_H

#include <vector>

struct dive;

/* BVM(3) probabilistic DCS scorer (Gerth & Vann, Undersea Hyperb. Med.
 * 24(4):275–292, 1997). Instantaneous risk is a weighted excess bubble
 * volume in three parallel-perfused N2 compartments. Air / N2-O2 only;
 * helium is not in the 1997 fit.
 *
 * Implementation is a port of deco/oai_bvm3.py (paper Appendix scheme).
 * Scores any dive profile after the fact (planner notes and the info
 * tab). Not a decompression controller. Helium is treated as N2. */
struct bvm3_node {
	double time_min = 0.0;
	double depth_fsw = 0.0;
	double fn2 = 0.79;
};

struct bvm3_result {
	bool valid = false;
	double pdcs = 0.0; /* probability in [0, 1] */
	int n_prior_dives = 0;
};

struct dive_table;

/* Score an explicit node list. A 24 h AIR surface interval is appended
 * until bubbles resolve; the last in-water mix is not continued. */
double bvm3_score_nodes(const std::vector<bvm3_node> &nodes,
			double surface_hours = 24.0);

extern bvm3_result calculate_bvm3_pdcs(const struct dive &dive, int dcNr,
				       const struct dive_table *table = nullptr);

#endif // BVM3_H
