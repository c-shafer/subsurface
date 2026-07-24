// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
#ifndef LEMSCORE_H
#define LEMSCORE_H

struct dive;

/* Probability of decompression sickness (pDCS) for a dive profile under
 * the U.S. Navy probabilistic LEM model (NEDU TR 02-10), scored with two
 * published calibrations:
 *   - LEM-he8n25   (TR 02-10 Table 4, the MK 16 MOD 1 He-O2 calibration)
 *   - LEM-HeN2_2016 (TR 23-11 Appendix B, the 2016 He/N2 re-optimization)
 * Values are probabilities in [0, 1]. `valid` is false when the dive has
 * no usable profile. See core/lemscore.cpp for scope and caveats. */
struct lem_pdcs_result {
	bool valid = false;
	double pdcs_he8n25 = 0.0;
	double pdcs_hen2_2016 = 0.0;
};

extern lem_pdcs_result calculate_lem_pdcs(const struct dive &dive, int dcNr);

#endif // LEMSCORE_H
