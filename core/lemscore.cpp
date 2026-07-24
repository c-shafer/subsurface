// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
/* pDCS scoring of dive profiles under the U.S. Navy probabilistic LEM
 * model (NEDU TR 02-10), using the vendored libdeco scorer in
 * core/deco-thalmann/deco/lem.h (layer A, the dissolved-gas hazard core;
 * bubble dynamics disabled - see that header).
 *
 * The dive's samples are converted to a LEM profile node list:
 * timestamped depth plus the breathing gas active at that moment. CCR
 * segments with a recorded setpoint are scored in constant-PO2 mode with
 * the diluent's He/N2 split; everything else (OC, PSCR, CCR without a
 * setpoint) is scored open-circuit on the active mix. Depth is converted
 * to the model's fsw convention (33 fsw = 1 atm) from gauge pressure
 * relative to the dive's own surface pressure, so altitude and salinity
 * are folded in the same way as the Thalmann planner backend.
 *
 * The scorer itself appends a 24 h post-dive surface interval (risk is
 * dominated by the surface phase); repetitive-dive interaction is NOT
 * modeled - each dive is scored in isolation from air saturation. */
#include "lemscore.h"
#include "dive.h"
#include "divecomputer.h"
#include "event.h"
#include "gas.h"
#include "range.h"
#include "sample.h"

#include "deco/lem.h"
#include "params/lem_he8n25.h"
#include "params/lem_hen2_2016.h"

/* Same conversion as core/thalmann.cpp: U.S. Navy 33 fsw = 1 atm. */
static const double fsw_per_bar = 33.0 / 1.01325;

static deco::LemProfile build_lem_profile(const struct dive &dive, const struct divecomputer &dc)
{
	deco::LemProfile profile;
	const double surface_bar = dive.get_surface_pressure().mbar / 1000.0;

	gasmix_loop loop_gas(dive, dc);
	divemode_loop loop_mode(dc);
	for (const auto &sample: dc.samples) {
		deco::LemNode node;
		node.time_min = sample.time.seconds / 60.0;
		double gauge_bar = dive.depth_to_bar(sample.depth) - surface_bar;
		node.depth_fsw = std::max(gauge_bar * fsw_per_bar, 0.0);

		auto [divemode, _cylinder_index, gasmix] = get_dive_status_at(dive, dc, sample.time.seconds, &loop_mode, &loop_gas);
		double fo2 = get_o2(*gasmix) / 1000.0;
		double fhe = get_he(*gasmix) / 1000.0;
		if (divemode == CCR && sample.setpoint.mbar > 0) {
			node.po2_setpoint = sample.setpoint.mbar / 1013.25;
			// He fraction of the non-O2 diluent balance (rest is N2).
			double inert = 1.0 - fo2;
			node.diluent_fhe = inert > 0.0 ? fhe / inert : 0.0;
		} else {
			node.gas = deco::GasMix{fo2, fhe};
		}
		profile.push_back(node);
	}
	return profile;
}

lem_pdcs_result calculate_lem_pdcs(const struct dive &dive, int dcNr)
{
	lem_pdcs_result result;
	const struct divecomputer *dc = dive.get_dc(dcNr);
	if (!dc)
		return result;

	// Manually entered dives without samples: score the synthesized
	// (faked) profile, same as the profile display does.
	struct divecomputer faked;
	if (dc->samples.size() < 2) {
		if (!dive.duration.seconds || !dive.maxdepth.mm)
			return result;
		faked = *dc;
		if (!faked.maxdepth.mm)
			faked.maxdepth = dive.maxdepth;
		if (!faked.duration.seconds)
			faked.duration = dive.duration;
		fake_dc(&faked);
		dc = &faked;
		if (dc->samples.size() < 2)
			return result;
	}

	deco::LemProfile profile = build_lem_profile(dive, *dc);
	if (profile.size() < 2)
		return result;

	static const deco::LemParams params_he8n25 = deco::params::lem_he8n25();
	static const deco::LemParams params_hen2_2016 = deco::params::lem_hen2_2016();

	result.pdcs_he8n25 = deco::lem_score(params_he8n25, profile).p_dcs;
	result.pdcs_hen2_2016 = deco::lem_score(params_hen2_2016, profile).p_dcs;
	result.valid = true;
	return result;
}
