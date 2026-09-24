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
 * A 24 h air surface tail is appended after the last dive (risk is
 * dominated by the surface phase). Preceding dives in the same 48 h /
 * same-trip window as init_decompression() are chained with air surface
 * intervals so residual inert gas is not reset between dives. */
#include "lemscore.h"
#include "dive.h"
#include "divecomputer.h"
#include "divelist.h"
#include "divelog.h"
#include "event.h"
#include "gas.h"
#include "range.h"
#include "sample.h"

#include <cmath>

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

static const struct divecomputer *lem_dc_or_fake(const struct dive &d, int dcNr, struct divecomputer &faked)
{
	const struct divecomputer *dc = d.get_dc(dcNr);
	if (!dc)
		dc = d.get_dc(0);
	if (!dc)
		return nullptr;
	if (dc->samples.size() >= 2)
		return dc;
	if (!d.duration.seconds || !d.maxdepth.mm)
		return nullptr;
	faked = *dc;
	if (!faked.maxdepth.mm)
		faked.maxdepth = d.maxdepth;
	if (!faked.duration.seconds)
		faked.duration = d.duration;
	fake_dc(&faked);
	return faked.samples.size() >= 2 ? &faked : nullptr;
}

static void lem_to_surface_air(deco::LemProfile &p)
{
	if (p.empty())
		return;
	deco::GasMix air{ 0.21, 0.0 };
	auto air_oc = [](const deco::LemNode &n) {
		return !n.po2_setpoint && n.gas.fhe < 0.01 && std::fabs(n.gas.fo2 - 0.21) < 0.02;
	};
	if (p.back().depth_fsw > 0.05) {
		deco::LemNode s;
		s.time_min = p.back().time_min + p.back().depth_fsw / 30.0;
		s.depth_fsw = 0.0;
		s.gas = air;
		p.push_back(s);
	} else if (!air_oc(p.back())) {
		deco::LemNode s;
		s.time_min = p.back().time_min + 1e-6;
		s.depth_fsw = 0.0;
		s.gas = air;
		p.push_back(s);
	}
}

static deco::LemProfile build_chained_lem_profile(const struct dive &dive, int dcNr,
						  const struct dive_table &table, int *n_prior)
{
	std::vector<const struct dive *> seq = table.preceding_repetitive_dives(&dive);
	if (n_prior)
		*n_prior = static_cast<int>(seq.size());
	seq.push_back(&dive);

	deco::LemProfile out;
	timestamp_t origin = seq.front()->when;
	for (size_t i = 0; i < seq.size(); i++) {
		const struct dive *d = seq[i];
		struct divecomputer faked;
		int use_dc = (d == &dive) ? dcNr : 0;
		const struct divecomputer *dc = lem_dc_or_fake(*d, use_dc, faked);
		if (!dc)
			continue;
		deco::LemProfile chunk = build_lem_profile(*d, *dc);
		if (chunk.size() < 2)
			continue;
		double t0 = (d->when - origin) / 60.0;
		if (t0 < 0.0)
			t0 = 0.0;
		for (auto &n: chunk)
			n.time_min += t0;
		if (!out.empty() && chunk.front().time_min + 1e-9 < out.back().time_min) {
			double shift = out.back().time_min - chunk.front().time_min;
			for (auto &n: chunk)
				n.time_min += shift;
		}
		if (!out.empty()) {
			lem_to_surface_air(out);
			double gap = chunk.front().time_min - out.back().time_min;
			if (gap > 1e-9) {
				deco::LemNode air;
				air.time_min = chunk.front().time_min;
				air.depth_fsw = 0.0;
				air.gas = deco::GasMix{ 0.21, 0.0 };
				out.push_back(air);
			}
		}
		for (auto &n: chunk)
			out.push_back(n);
	}
	return out;
}

lem_pdcs_result calculate_lem_pdcs(const struct dive &dive, int dcNr, const struct dive_table *table)
{
	lem_pdcs_result result;
	const struct dive_table &tab = table ? *table : divelog.dives;
	deco::LemProfile profile = build_chained_lem_profile(dive, dcNr, tab, &result.n_prior_dives);
	if (profile.size() < 2)
		return result;

	static const deco::LemParams params_he8n25 = deco::params::lem_he8n25();
	static const deco::LemParams params_hen2_2016 = deco::params::lem_hen2_2016();

	result.pdcs_he8n25 = deco::lem_score(params_he8n25, profile).p_dcs;
	result.pdcs_hen2_2016 = deco::lem_score(params_hen2_2016, profile).p_dcs;
	result.valid = true;
	return result;
}
