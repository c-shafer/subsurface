// SPDX-License-Identifier: GPL-2.0
// AI-generated (Claude)
/* BVM(3) scorer: port of deco/oai_bvm3.py, which implements the
 * published 3-compartment Bubble Volume Model in
 *   Gerth WA, Vann RD. Probabilistic gas and bubble dynamics models of
 *   DCS occurrence in air and N2-O2 diving. Undersea Hyperb Med.
 *   1997;24:275-292
 * (Methods + Appendix: Eqs. 1, 2, A1–A6, A12; Tables 4 and A1).
 *
 * Instantaneous risk is Eq. 2 as G_i * max(V_b,i - V0_b,i, 0) — Table 4
 * G units are (min*cm^3)^-1, so the time integral in Eq. 1 is
 * dimensionless. Compartments are independent until those hazards are
 * summed. A post-dive AIR surface interval is run until each bubble
 * resolves, capped at surface_hours (24 h by default). Surface gas is
 * not the last in-water mix: continuing O2 after surfacing halves
 * AIR/O2 Appendix E scores.
 *
 * Numerical scheme matches oai_bvm3.py / the paper Appendix:
 *   no bubble  — analytic Schreiner (Eq. A3)
 *   dPamb      — full elastic Boyle jump (Eq. A6)
 *   bubble on  — trapezoidal A4 + A2 with d(Pb Vb) mass balance
 *   dt         — Eq. A12 (dt_min, dt_max, nu)
 *   nucleate   — Pamb < Pt + Pmg - 2σ/r0 and dr/dt(r0) > 0
 *   extinguish — r back at r0 while still shrinking
 *
 * Preceding dives in the same 48 h / same-trip window as
 * init_decompression() are chained with air surface intervals; the 24 h
 * air tail runs after the last dive.
 *
 * Scope: one diffusible gas (N2). Helium profiles are scored as if the
 * non-O2 fraction were N2, which is outside the 1997 calibration. Not a
 * decompression controller. */
#include "bvm3.h"
#include "dive.h"
#include "divecomputer.h"
#include "divelist.h"
#include "divelog.h"
#include "event.h"
#include "gas.h"
#include "sample.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static const double pi = 3.14159265358979323846;
static const int ncomp = 3;

/* Table footnote: 1 atm = 33.066 fsw. 1 atm = 1.01325 bar. */
static const double fsw_per_atm = 33.066;
static const double fsw_per_bar = fsw_per_atm / 1.01325;

static const double ph2o_atm = 47.0 / 760.0;
static const double pmg_atm = (47.0 + 45.0 + 25.0) / 760.0;
static const double sigma_atm_cm = 30.0 / 1.01325e6;
static const double ps_atm = 1.0;
static const double fn2_air = 0.79;

/* Eq. A12 */
static const double dt_min = 0.005;
static const double dt_max = 0.100;
static const double nu_a12 = -6.93e5; /* cm^-1 */

struct BVM3Comp {
	double G;       /* (min*cm^3)^-1 */
	double M;       /* atm/cm^3 */
	double r0;      /* cm */
	double alpha_t; /* Ostwald */
	double V_t;     /* cm^3 */
	double D_t;     /* cm^2/min */
	double half_time; /* min, Table 4 printed derived value */
	double tau() const { return half_time / std::log(2.0); }
};

/* Table 4 BVM(3) column as used by oai_bvm3.py. tau is taken from the
 * printed half-time so rounded alpha_t, V_t, alpha_b_Q are not applied
 * twice. */
static const BVM3Comp BVM3[ncomp] = {
	{ 7.09e2, 1.74e5, 5.57e-3, 2.54e-5, 2.73e-2, 1.48e1, 1.00 },
	{ 1.04e2, 1.94e5, 6.62e-3, 5.94e-5, 6.16e-1, 1.38e-3, 26.40 },
	{ 5.85e2, 7.93e5, 1.32e-3, 2.84e-3, 1.13e-2, 1.12e-1, 316.62 },
};

struct Stage {
	double depth0 = 0.0;
	double depth1 = 0.0;
	double duration = 0.0;
	double fn20 = fn2_air;
	double fn21 = fn2_air;
	bool isobaric() const { return std::fabs(depth1 - depth0) < 1e-14; }
};

static double sphere_vol(double r)
{
	return 4.0 / 3.0 * pi * r * r * r;
}

static double radius_from_vol(double v)
{
	if (v <= 0.0)
		return 0.0;
	return std::cbrt(3.0 * v / (4.0 * pi));
}

static double pamb_from_fsw(double depth_fsw)
{
	return 1.0 + std::max(depth_fsw, 0.0) / fsw_per_atm;
}

static double alveolar_n2(double depth_fsw, double fn2)
{
	double fn = std::clamp(fn2, 0.0, 1.0);
	return fn * std::max(pamb_from_fsw(depth_fsw) - ph2o_atm, 0.0);
}

static double pb_atm(double r, double pamb, const BVM3Comp &c)
{
	if (r <= 0.0)
		return 0.0;
	return pamb - pmg_atm + 2.0 * sigma_atm_cm / r + c.M * sphere_vol(r);
}

static double dr_dt(double r, double pt, double pamb, const BVM3Comp &c)
{
	double lam = 1.0 / std::sqrt(c.tau() * c.D_t);
	double pb = pb_atm(r, pamb, c);
	if (pb < 1e-12)
		pb = 1e-12;
	return -c.alpha_t * c.D_t * ps_atm * (1.0 - pt / pb) * (lam + 1.0 / r);
}

static bool formation_condition(double pt, double pamb, const BVM3Comp &c)
{
	double p_form = pt + pmg_atm - 2.0 * sigma_atm_cm / c.r0;
	return pamb < p_form;
}

static double schreiner(double pt0, double pa0, double ka, double dt, double tau)
{
	double e = std::exp(-dt / tau);
	return pa0 + ka * (dt - tau) - (pa0 - pt0 - ka * tau) * e;
}

/* Eq. A6 (M_i > 0 for all BVM(3) compartments). */
static double boyle_elastic_volume(double v_old, double pamb_old, double pamb_new,
				   const BVM3Comp &c)
{
	double a = pamb_new - pmg_atm;
	double b = pamb_old - pmg_atm;
	if (v_old <= 0.0 || a * a < 1e-30)
		return v_old;
	double inside = 1.0 + 4.0 * c.M * v_old * (b + c.M * v_old) / (a * a);
	if (inside < 0.0)
		inside = 0.0;
	return (a / (2.0 * c.M)) * (-1.0 + std::sqrt(inside));
}

static double tissue_trapezoid(double pt0, double pa0, double pa1, double delta_pb_vb,
			      double dt, const BVM3Comp &c)
{
	double a = dt / (2.0 * c.tau());
	double rhs = pt0 + a * (pa0 - pt0 + pa1) - delta_pb_vb / (c.alpha_t * c.V_t);
	return rhs / (1.0 + a);
}

static double v0_of(const BVM3Comp &c)
{
	return sphere_vol(c.r0);
}

static double score_compartment(const std::vector<Stage> &stages, const BVM3Comp &c,
				double surface_fn2, double max_post_surface_min,
				double *r_end_out)
{
	double fn2_initial = stages.empty() ? fn2_air : stages.front().fn20;
	double pt = alveolar_n2(0.0, fn2_initial);
	bool active = false;
	double r = c.r0;
	double hazard = 0.0;
	double t = 0.0;
	const double v0 = v0_of(c);

	auto one_step = [&](double depth0, double depth1, double fn20, double fn21, double dt) {
		if (dt <= 0.0)
			return;
		double p0 = pamb_from_fsw(depth0);
		double p1 = pamb_from_fsw(depth1);
		double pa0 = alveolar_n2(depth0, fn20);
		double pa1 = alveolar_n2(depth1, fn21);

		double risk0 = active ? c.G * std::max(0.0, sphere_vol(r) - v0) : 0.0;

		if (!active) {
			double ka = (pa1 - pa0) / dt;
			double pt1 = schreiner(pt, pa0, ka, dt, c.tau());
			bool grows0 = formation_condition(pt, p0, c) && dr_dt(c.r0, pt, p0, c) > 0.0;
			bool grows1 = formation_condition(pt1, p1, c) && dr_dt(c.r0, pt1, p1, c) > 0.0;
			pt = pt1;
			if (grows0 || grows1) {
				active = true;
				r = c.r0;
			}
		} else {
			double v_old = sphere_vol(r);
			double r_boyle = r;
			if (std::fabs(p1 - p0) > 1e-15) {
				double v_boyle = boyle_elastic_volume(v_old, p0, p1, c);
				r_boyle = radius_from_vol(v_boyle);
			}
			double q_boyle = pb_atm(r_boyle, p1, c) * sphere_vol(r_boyle);
			double f0 = dr_dt(r_boyle, pt, p1, c);
			double r1 = std::max(c.r0, r_boyle + f0 * dt);
			double pt1 = pt;
			for (int it = 0; it < 50; it++) {
				double q1 = pb_atm(r1, p1, c) * sphere_vol(r1);
				double new_pt1 = tissue_trapezoid(pt, pa0, pa1, q1 - q_boyle, dt, c);
				double f1 = dr_dt(r1, new_pt1, p1, c);
				double new_r1 = std::max(c.r0, r_boyle + 0.5 * (f0 + f1) * dt);
				if (std::fabs(new_r1 - r1) < 1e-13 && std::fabs(new_pt1 - pt1) < 1e-11) {
					r1 = new_r1;
					pt1 = new_pt1;
					break;
				}
				r1 = new_r1;
				pt1 = new_pt1;
			}
			bool resolving = r1 <= c.r0 * (1.0 + 1e-12) && f0 < 0.0;
			r = r1;
			pt = pt1;
			if (resolving) {
				active = false;
				r = c.r0;
			}
		}

		double risk1 = active ? c.G * std::max(0.0, sphere_vol(r) - v0) : 0.0;
		hazard += 0.5 * (risk0 + risk1) * dt;
		t += dt;
	};

	for (const auto &stage: stages) {
		double elapsed = 0.0;
		while (elapsed < stage.duration - 1e-12) {
			double dt;
			if (!stage.isobaric())
				dt = std::min(dt_min, stage.duration - elapsed);
			else if (active) {
				double rate = std::fabs(dr_dt(r, pt, pamb_from_fsw(stage.depth0), c));
				double dt_adapt = (dt_max - dt_min) * std::exp(nu_a12 * rate) + dt_min;
				dt = std::min(std::max(dt_min, dt_adapt), dt_max);
				dt = std::min(dt, stage.duration - elapsed);
			} else
				dt = std::min(dt_min, stage.duration - elapsed);

			double x0 = elapsed / stage.duration;
			double x1 = (elapsed + dt) / stage.duration;
			double d0 = stage.depth0 + (stage.depth1 - stage.depth0) * x0;
			double d1 = stage.depth0 + (stage.depth1 - stage.depth0) * x1;
			double f0 = stage.fn20 + (stage.fn21 - stage.fn20) * x0;
			double f1 = stage.fn20 + (stage.fn21 - stage.fn20) * x1;
			one_step(d0, d1, f0, f1, dt);
			elapsed += dt;
		}
	}

	/* Post-surface gas is an explicit input (air). Do not continue the
	 * last in-water mix: O2 deco followed by O2 at the surface collapses
	 * the bubble-resolution tail (oai_bvm3.py / IMPLEMENTATION_NOTES). */
	double post = 0.0;
	while (active && post < max_post_surface_min) {
		double rate = std::fabs(dr_dt(r, pt, 1.0, c));
		double dt = (dt_max - dt_min) * std::exp(nu_a12 * rate) + dt_min;
		dt = std::min(dt, dt_max);
		dt = std::min(dt, max_post_surface_min - post);
		dt = std::max(dt_min, dt);
		one_step(0.0, 0.0, surface_fn2, surface_fn2, dt);
		post += dt;
	}

	if (r_end_out)
		*r_end_out = active ? r : 0.0;
	(void)t;
	return hazard;
}

static std::vector<Stage> stages_from_nodes(const std::vector<bvm3_node> &nodes)
{
	std::vector<bvm3_node> p = nodes;
	std::sort(p.begin(), p.end(), [](const bvm3_node &a, const bvm3_node &b) {
		return a.time_min < b.time_min;
	});
	if (p.back().depth_fsw > 0.05) {
		bvm3_node surf = p.back();
		surf.time_min += std::max(p.back().depth_fsw, 0.0) / 30.0;
		surf.depth_fsw = 0.0;
		p.push_back(surf);
	}

	std::vector<Stage> stages;
	for (size_t i = 1; i < p.size(); i++) {
		double dt = p[i].time_min - p[i - 1].time_min;
		if (dt <= 1e-15)
			continue;
		Stage s;
		s.depth0 = p[i - 1].depth_fsw;
		s.depth1 = p[i].depth_fsw;
		s.duration = dt;
		s.fn20 = p[i - 1].fn2;
		s.fn21 = p[i].fn2;
		stages.push_back(s);
	}
	return stages;
}

double bvm3_score_nodes(const std::vector<bvm3_node> &nodes, double surface_hours)
{
	if (nodes.size() < 2)
		return 0.0;
	std::vector<Stage> stages = stages_from_nodes(nodes);
	if (stages.empty())
		return 0.0;

	double max_post = std::max(surface_hours, 0.0) * 60.0;
	double H = 0.0;
	double r_end[ncomp] = { 0, 0, 0 };
	for (int i = 0; i < ncomp; i++)
		H += score_compartment(stages, BVM3[i], fn2_air, max_post, &r_end[i]);

	if (H < 0.0)
		H = 0.0;
	if (H > 50.0)
		H = 50.0;
	if (getenv("BVM3_DEBUG"))
		fprintf(stderr, "BVM3 H=%.6f r=[%.4g %.4g %.4g]\n", H, r_end[0], r_end[1], r_end[2]);
	return 1.0 - std::exp(-H);
}

static const struct divecomputer *bvm3_dc_or_fake(const struct dive &d, int dcNr, struct divecomputer &faked)
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

static void bvm3_to_surface_air(std::vector<bvm3_node> &p)
{
	if (p.empty())
		return;
	if (p.back().depth_fsw > 0.05) {
		bvm3_node s = p.back();
		s.time_min += std::max(p.back().depth_fsw, 0.0) / 30.0;
		s.depth_fsw = 0.0;
		s.fn2 = fn2_air;
		p.push_back(s);
	} else if (std::fabs(p.back().fn2 - fn2_air) > 1e-4) {
		bvm3_node s = p.back();
		s.time_min += 1e-6;
		s.depth_fsw = 0.0;
		s.fn2 = fn2_air;
		p.push_back(s);
	}
}

static std::vector<bvm3_node> nodes_from_dive(const struct dive &dive, const struct divecomputer &dc)
{
	std::vector<bvm3_node> nodes;
	const double surface_bar = dive.get_surface_pressure().mbar / 1000.0;
	gasmix_loop loop_gas(dive, dc);
	divemode_loop loop_mode(dc);
	for (const auto &sample: dc.samples) {
		bvm3_node n;
		n.time_min = sample.time.seconds / 60.0;
		double gauge_bar = dive.depth_to_bar(sample.depth) - surface_bar;
		n.depth_fsw = std::max(gauge_bar * fsw_per_bar, 0.0);
		auto [divemode, _cyl, gasmix] = get_dive_status_at(dive, dc, sample.time.seconds, &loop_mode, &loop_gas);
		double fo2 = get_o2(*gasmix) / 1000.0;
		double fhe = get_he(*gasmix) / 1000.0;
		n.fn2 = std::max(1.0 - fo2 - fhe, 0.0);
		(void)divemode;
		nodes.push_back(n);
	}
	return nodes;
}

static std::vector<bvm3_node> build_chained_bvm3_nodes(const struct dive &dive, int dcNr,
						       const struct dive_table &table, int *n_prior)
{
	std::vector<const struct dive *> seq = table.preceding_repetitive_dives(&dive);
	if (n_prior)
		*n_prior = static_cast<int>(seq.size());
	seq.push_back(&dive);

	std::vector<bvm3_node> out;
	timestamp_t origin = seq.front()->when;
	for (size_t i = 0; i < seq.size(); i++) {
		const struct dive *d = seq[i];
		struct divecomputer faked;
		int use_dc = (d == &dive) ? dcNr : 0;
		const struct divecomputer *dc = bvm3_dc_or_fake(*d, use_dc, faked);
		if (!dc)
			continue;
		std::vector<bvm3_node> chunk = nodes_from_dive(*d, *dc);
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
			bvm3_to_surface_air(out);
			double gap = chunk.front().time_min - out.back().time_min;
			if (gap > 1e-9) {
				bvm3_node air = out.back();
				air.time_min = chunk.front().time_min;
				air.depth_fsw = 0.0;
				air.fn2 = fn2_air;
				out.push_back(air);
			}
		}
		out.insert(out.end(), chunk.begin(), chunk.end());
	}
	return out;
}

bvm3_result calculate_bvm3_pdcs(const struct dive &dive, int dcNr, const struct dive_table *table)
{
	bvm3_result result;
	const struct dive_table &tab = table ? *table : divelog.dives;
	std::vector<bvm3_node> nodes = build_chained_bvm3_nodes(dive, dcNr, tab, &result.n_prior_dives);
	if (nodes.size() < 2)
		return result;
	if (getenv("BVM3_DEBUG")) {
		double mx = 0;
		for (auto &n: nodes)
			if (n.depth_fsw > mx)
				mx = n.depth_fsw;
		fprintf(stderr, "BVM3 profile n=%zu tmax=%.2f dmax=%.1f last=%.1f fn2=%.3f prior=%d\n",
			nodes.size(), nodes.back().time_min, mx, nodes.back().depth_fsw, nodes.back().fn2,
			result.n_prior_dives);
	}
	result.pdcs = bvm3_score_nodes(nodes);
	result.valid = std::isfinite(result.pdcs);
	return result;
}
