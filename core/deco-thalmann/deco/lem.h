// libdeco/include/deco/lem.h
//
// LEM — the probabilistic Linear-Exponential Multigas decompression model
// of Gerth & Johnson, NEDU Technical Report 02-10 (2002), "Development and
// Validation of 1.3 ATA PO2-in-He Decompression Tables for the MK 16 MOD 1
// UBA" (DTIC accession ADA407841). All model equations referenced below by
// their Appendix A numbers (e.g. "A9") are from that report.
//
// This is a self-contained probabilistic scorer: given a dive profile it
// computes P_DCS, the estimated probability of decompression sickness. It is
// distinct from the deterministic EL-DCM / Workman-MPTT machinery in
// eldcm.h (which drives the XVal-He table generation) — the LEM model has
// its own compartment structure, its own per-gas kinetics, and its own
// hazard integral.
//
// SCOPE (as of this writing) — LAYER A, the dissolved-gas hazard core:
//   arterial equilibration (A1/A2/A7), PSET-gated dynamic O2 (A5/A6),
//   bubble-free exponential kinetics (A9), supersaturation (A27.a),
//   instantaneous hazard (A27), and the cumulative-risk integral (A26).
//   This layer is fully pinned by TR 02-10 Table 4 and is exactly
//   reproducible. The bubble-dynamics branch (nucleation + the nonlinear
//   multigas root solve, A10-A25) is LAYER B and is NOT implemented here;
//   `LemScore::bubble_regime_min` reports, per compartment, how long the
//   profile spent in the regime where layer B physics would engage, so a
//   caller can see when this layer-A score is an approximation. See DEVLOG.
//
// UNIT CONVENTION: the LEM model (and Table 4) is native in ATMOSPHERES
// (ATA/atm) for pressures and tensions, minutes for time. Depth is accepted
// in fsw (to match the rest of libdeco and profile.h's GasMix) and converted
// to atm at the boundary via LemParams::surface_pressure_fsw. Nothing inside
// the engine mixes unit systems.
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "deco/profile.h"

namespace deco {

// ---------------------------------------------------------------------------
// Parameters (TR 02-10 Table 4)
// ---------------------------------------------------------------------------

// One modeled compartment ("tissue"). Time constants are TC (the exponential
// e-folding time, min) — NOT half-times (t_1/2 = ln2 * TC, A3.c). Gas order
// is O2, N2, He throughout.
struct LemCompartment {
    // TC(t): exchange time constants, min (A9's tau_i,g).
    double tc_o2_min = 1.0;
    double tc_n2_min = 1.0;
    double tc_he_min = 1.0;

    // alphat(t): tissue Ostwald solubilities, ml/ml/atm. Used by the layer-B
    // bubble solve (A19-A25); recorded here for completeness even though the
    // layer-A core does not consume them.
    double at_o2 = 0.0;
    double at_n2 = 0.0;
    double at_he = 0.0;

    // PSET: O2 tracking threshold, ATA (A5/A6). Alveolar O2 above this
    // contributes dynamically to the compartment's inert burden; a very large
    // value (Table 4 uses 99 ATA for compartments 1 and 3) forces O2 to be
    // static (j=0) so O2 never contributes to that compartment.
    double pset_ata = 99.0;
    // (at_o2/at_he are 0 in Table 4; layer B substitutes at_n2 for them.)

    // PXO: E->L kinetic (bubble-formation) threshold, atm (A12's PXO_i).
    // Layer-B parameter; layer A uses it only to report bubble-regime time.
    double pxo_atm = 1.0e10;

    // THR: risk threshold, atm (A27). Supersaturation above this drives
    // hazard in this compartment.
    double thr_atm = 0.0;

    // G_i: constant compartmental gain, 1/min (A27). See LemParams for the
    // Table 4 "Gain/10**3" scaling note.
    double gain_per_min = 0.0;
};

struct LemParams {
    std::string name;

    // Global physiological constants. Table 4 gives PTH2O, PTCO2 (= PVCO2),
    // PTO2 (= PVO2) and PACO2 in mm-Hg; store them in atm (1 atm = 760 mmHg).
    double rq = 1.0;         // respiratory quotient
    double pth2o_atm = 0.0;  // tissue water vapor
    double ptco2_atm = 0.0;  // tissue (== venous) CO2
    double pto2_atm = 0.0;   // tissue (== venous) O2 (used only where O2 static)
    double paco2_atm = 0.0;  // arterial CO2

    // alphab: blood Ostwald solubilities, ml/ml (A18/A19, layer B).
    double alphab_o2 = 0.0;
    double alphab_n2 = 0.0;
    double alphab_he = 0.0;

    std::vector<LemCompartment> compartments;

    // fsw <-> atm boundary conversion. Pamb(atm) = 1 + depth_fsw/surfp.
    double surface_pressure_fsw = 33.0;

    // Pre-dive surface saturation gas (the mix the diver was breathing at the
    // surface before descent). Default air. Tissue tensions are initialized to
    // equilibrium with this at surface pressure.
    double init_fo2 = 0.21;
    double init_fhe = 0.0;

    // Layer B — bubble dynamics (TR 02-10 A10-A25). When true, once a
    // compartment's supersaturation exceeds its PXO threshold a bubble
    // nucleates; the burden P' thereafter washes out at the perfusion rate
    // against the bubble-clamped dissolved tension (A10/A13/A20), sustaining
    // supersaturation and driving more risk than the bubble-free layer-A
    // kinetics. Off by default: layer A is the validated, source-pinned core
    // (see DEVLOG); layer B is opt-in and uses alpha_blood as the bubble
    // solubility because Table 4's alpha_t is 0 for He/O2 (a likely typo —
    // n/alpha_t would be undefined; see DEVLOG "Phase 3 — layer B").
    bool enable_bubbles = false;

    // Numerical integration / kinetics step, min (dt_min). Quadrature
    // resolution for the hazard integral; segments are subdivided so no step
    // exceeds this. The report prescribes step-wise processing during risk
    // accumulation but does not state a value — set here by project decision
    // to 1 second. Quiescent periods (isobaric holds, the washed-out portion
    // of the surface tail) are fast-forwarded analytically, so this fine step
    // costs little: it applies only during ascent transients and the early
    // post-surface window where risk actually accrues.
    double dt_min = 1.0 / 60.0;  // 1 second

    // NOTE: layer B uses N2's tissue solubility (alpha_t = 0.073) for He and
    // O2 as well, since Table 4's alpha_t is 0 for those (undefined in
    // P' = n/alpha_t). Empirically the deep-end result is nearly identical to
    // using alpha_blood, so the residual deep gap is not driven by this
    // choice. See lem-equations.h / DEVLOG "Phase 3".

    // Post-dive surface interval appended by lem_score(), min. DCS risk is
    // dominated by the surface phase (ambient at 1 atm, supersaturation
    // highest), so a dive log that ends at surfacing must be integrated
    // forward until tissues wash out below threshold. lem_score() appends an
    // air segment of this length at the surface after the profile's final
    // node, but ONLY if that final node is already at the surface (a truncated
    // log that ends at depth is integrated as-is, without fabricating an
    // ascent). Default 1440 min (24 h) — long enough for the slow compartment
    // to stop accumulating; set 0 to score exactly the given profile.
    double surface_tail_min = 1440.0;
};

// ---------------------------------------------------------------------------
// Profile input (dive-log style)
// ---------------------------------------------------------------------------

// One node of a LEM dive profile. `time_min` is ABSOLUTE elapsed time from
// dive start (dive-log timestamps, converted to minutes-since-start). Depth
// in fsw. Between consecutive nodes, depth and inspired gas fractions are
// interpolated linearly in time (TR 02-10 Appendix A, "Implementation").
//
// Gas is given one of two ways:
//   - Open circuit: set `gas.fo2` / `gas.fhe` (constant inspired fractions).
//   - Closed-circuit rebreather: set `po2_setpoint` (atm) and `diluent_fhe`
//     (He fraction of the non-O2 diluent balance; the rest is N2). The
//     inspired O2 fraction is derived from depth at resolve time.
struct LemNode {
    double time_min = 0.0;
    double depth_fsw = 0.0;
    GasMix gas;                          // open-circuit inspired mix
    std::optional<double> po2_setpoint;  // CCR setpoint, atm
    double diluent_fhe = 0.0;            // CCR: He fraction of diluent (rest N2)
};

using LemProfile = std::vector<LemNode>;

// A profile node reduced to what the engine consumes: absolute time, ambient
// pressure (atm), and inspired fractions. fn2 = 1 - fo2 - fhe.
struct LemResolvedNode {
    double time_min = 0.0;
    double pamb_atm = 1.0;
    double fo2 = 0.21;
    double fhe = 0.0;
};

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

struct LemScore {
    double p_dcs = 0.0;             // A26: 1 - exp(-integral h dt)
    double hazard_integral = 0.0;   // integral of h(t) over the profile
    double total_time_min = 0.0;

    // Per-compartment cumulative hazard contribution (sums to hazard_integral).
    std::vector<double> compartment_hazard;

    // Per-compartment time (min) spent with SS_i > PXO_i, i.e. in the regime
    // where the (unimplemented) layer-B bubble kinetics would engage. Nonzero
    // values flag that this layer-A score is an approximation there.
    std::vector<double> bubble_regime_min;
};

// ---------------------------------------------------------------------------
// Engine internals
// ---------------------------------------------------------------------------

inline double lem_pamb_atm(const LemParams& p, double depth_fsw) {
    return 1.0 + depth_fsw / p.surface_pressure_fsw;
}

// Resolve a raw dive-log profile into (time, Pamb, inspired-fraction) nodes.
// For CCR nodes, the inspired O2 fraction is computed from the setpoint at the
// node's own depth and the non-O2 balance is split by the diluent He fraction
// — matching the report's scheme of a per-node inspired O2 fraction that is
// then linearly interpolated between nodes.
inline std::vector<LemResolvedNode> lem_resolve(const LemParams& p,
                                                 const LemProfile& profile) {
    std::vector<LemResolvedNode> out;
    out.reserve(profile.size());
    for (const LemNode& n : profile) {
        LemResolvedNode r;
        r.time_min = n.time_min;
        r.pamb_atm = lem_pamb_atm(p, n.depth_fsw);
        if (n.po2_setpoint) {
            double fo2 = *n.po2_setpoint / r.pamb_atm;
            fo2 = std::clamp(fo2, 0.0, 1.0);
            double balance = 1.0 - fo2;
            r.fo2 = fo2;
            r.fhe = balance * n.diluent_fhe;
        } else {
            r.fo2 = n.gas.fo2;
            r.fhe = n.gas.fhe;
        }
        out.push_back(r);
    }
    return out;
}

// Bracket B = (Pamb - PH2O) - PACO2*(1 - 1/RQ), the driving pressure that
// inspired inert fractions scale in the alveolar/arterial equation (A2).
inline double lem_arterial_bracket(const LemParams& p, double pamb_atm) {
    double rq_term = (p.rq != 0.0) ? p.paco2_atm * (1.0 - 1.0 / p.rq) : 0.0;
    return (pamb_atm - p.pth2o_atm) - rq_term;
}

// Alveolar O2 partial pressure (A7): Pamb - PH2O - PACO2 - sum(inert arterial).
inline double lem_alveolar_po2(const LemParams& p, double pamb_atm, double fn2,
                               double fhe) {
    double bracket = lem_arterial_bracket(p, pamb_atm);
    double pa_n2 = fn2 * bracket;
    double pa_he = fhe * bracket;
    return pamb_atm - p.pth2o_atm - p.paco2_atm - (pa_n2 + pa_he);
}

// A9 over one interval of length dt with the arterial tension going linearly
// from pa0 to pa1. Returns the new tissue tension.
inline double lem_advance_tension(double p_old, double pa0, double pa1,
                                  double tau_min, double dt) {
    if (dt <= 0.0) return p_old;
    if (tau_min <= 0.0) return pa1;  // instantaneous equilibration guard
    double e = std::exp(-dt / tau_min);
    double k = (pa1 - pa0) / dt;  // rate of change of arterial tension
    return pa0 + (p_old - pa0) * e + k * dt + k * tau_min * (e - 1.0);
}

// Tissue-gas tensions for one compartment: [O2-excess, N2, He], atm.
struct LemTissue {
    double o2 = 0.0;
    double n2 = 0.0;
    double he = 0.0;
    double burden() const { return o2 + n2 + he; }  // P'_t,i in the bubble-free case
};

// Bubble state for one compartment (layer B). When active, `bp_*` hold the
// per-gas inert-gas burden P'_g (A21: P'_g = n_g/alpha_g); the dissolved
// tensions are recovered as p_t,g = alpha_g P'_g / (alpha_g + X).
struct LemBubble {
    bool active = false;
    double bp_o2 = 0.0;
    double bp_n2 = 0.0;
    double bp_he = 0.0;
    double burden() const { return bp_o2 + bp_n2 + bp_he; }
};

// Solve the bubble constraint (A13/A22): find X = V_b/RT >= 0 such that the
// sum of dissolved tensions equals A = Pamb - p_fix + PXO, given the per-gas
// burdens P'_g and solubilities alpha_g:
//     sum_g  alpha_g P'_g / (alpha_g + X)  =  A .
// The left side is monotone-decreasing in X (from sum P'_g at X=0 toward 0),
// so a unique root exists whenever sum P'_g > A. Returns 0 when the tissue is
// no longer supersaturated enough to hold a bubble (bubble resolves). This
// monotone root-find is equivalent to, and more robust than, the explicit
// quadratic (A24) / Cardano cubic (A25) the report gives.
inline double lem_bubble_solve_X(double A, const double* alpha,
                                 const double* pprime, int ngas) {
    double sum_pp = 0.0;
    for (int g = 0; g < ngas; ++g) sum_pp += pprime[g];
    if (sum_pp <= A) return 0.0;  // not supersaturated past threshold
    auto f = [&](double x) {
        double s = 0.0;
        for (int g = 0; g < ngas; ++g) s += alpha[g] * pprime[g] / (alpha[g] + x);
        return s - A;
    };
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 60 && f(hi) > 0.0; ++i) hi *= 2.0;
    for (int i = 0; i < 80; ++i) {
        double mid = 0.5 * (lo + hi);
        if (f(mid) > 0.0)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

// ---------------------------------------------------------------------------
// Scorer
// ---------------------------------------------------------------------------

// Scores a dive-log profile for P_DCS under the LEM model (layer A). The
// profile must have >= 1 node; times must be non-decreasing.
inline LemScore lem_score(const LemParams& p, const LemProfile& profile) {
    const std::size_t nc = p.compartments.size();
    LemScore score;
    score.compartment_hazard.assign(nc, 0.0);
    score.bubble_regime_min.assign(nc, 0.0);
    if (profile.empty() || nc == 0) return score;

    std::vector<LemResolvedNode> nodes = lem_resolve(p, profile);

    // Append the post-dive surface interval (see LemParams::surface_tail_min).
    // Only when the log already ends at the surface — never fabricate an
    // ascent for a fragment that ends at depth. The switch to surface air is
    // instantaneous (zero-length segment, skipped by the integrator).
    if (p.surface_tail_min > 0.0 && !nodes.empty() &&
        std::abs(nodes.back().pamb_atm - 1.0) < 1e-6) {
        const double t_end = nodes.back().time_min;
        LemResolvedNode air{t_end, 1.0, p.init_fo2, p.init_fhe};
        nodes.push_back(air);
        air.time_min = t_end + p.surface_tail_min;
        nodes.push_back(air);
    }

    // Initial tissue state: equilibrium with the pre-dive surface saturation
    // gas at surface pressure. Bubbles start inactive.
    const double psurf = 1.0;  // atm
    const double init_fn2 = 1.0 - p.init_fo2 - p.init_fhe;
    const double init_bracket = lem_arterial_bracket(p, psurf);
    const double init_pao2 =
        lem_alveolar_po2(p, psurf, init_fn2, p.init_fhe);
    std::vector<LemTissue> tissue(nc);
    std::vector<LemBubble> bubble(nc);
    for (std::size_t i = 0; i < nc; ++i) {
        const LemCompartment& c = p.compartments[i];
        tissue[i].n2 = init_fn2 * init_bracket;
        tissue[i].he = p.init_fhe * init_bracket;
        tissue[i].o2 = std::max(0.0, init_pao2 - c.pset_ata);
    }

    // p_fix (A4): fixed-gas tissue tensions. PTO2 is included only where O2 is
    // static (large PSET); where O2 tracks dynamically it is already in the
    // burden. All are 0 in Table 4.
    auto p_fix_of = [&](const LemCompartment& c) {
        const bool o2_static = c.pset_ata > 50.0;
        return p.pth2o_atm + p.ptco2_atm + (o2_static ? p.pto2_atm : 0.0);
    };
    // Compartmental inert-gas burden P'_t,i: the sum of dissolved tensions
    // (bubble-free), or the sum of per-gas burdens once a bubble is present.
    auto burden_of = [&](std::size_t i) {
        return bubble[i].active ? bubble[i].burden() : tissue[i].burden();
    };

    // Per-time-instant instantaneous hazard (A27), split by compartment.
    auto hazard_at = [&](double pamb, std::vector<double>& per_cmpt) {
        double h = 0.0;
        for (std::size_t i = 0; i < nc; ++i) {
            const LemCompartment& c = p.compartments[i];
            const double ss = burden_of(i) - (pamb - p_fix_of(c));  // A27.a
            const double over = ss - c.thr_atm;
            if (over > 0.0) {
                double hi = c.gain_per_min * over / pamb;  // A27
                per_cmpt[i] = hi;
                h += hi;
            } else {
                per_cmpt[i] = 0.0;
            }
        }
        return h;
    };

    std::vector<double> h0_cmpt(nc, 0.0), h1_cmpt(nc, 0.0);

    for (std::size_t seg = 0; seg + 1 < nodes.size(); ++seg) {
        const LemResolvedNode& a = nodes[seg];
        const LemResolvedNode& b = nodes[seg + 1];
        const double seg_len = b.time_min - a.time_min;
        if (seg_len <= 0.0) continue;
        const int steps =
            std::max(1, static_cast<int>(std::ceil(seg_len / p.dt_min - 1e-9)));
        const double dt = seg_len / steps;

        // A segment with unchanging pressure and inspired gas (an isobaric
        // hold or the post-dive surface tail) has constant arterial tensions,
        // so each compartment relaxes monotonically toward them — enabling the
        // quiescent fast-forward below.
        const bool constant_seg =
            std::abs(a.pamb_atm - b.pamb_atm) < 1e-12 &&
            std::abs(a.fo2 - b.fo2) < 1e-12 && std::abs(a.fhe - b.fhe) < 1e-12;

        for (int s = 0; s < steps; ++s) {
            const double f0 = static_cast<double>(s) / steps;
            const double f1 = static_cast<double>(s + 1) / steps;
            // Linear interpolation of Pamb and inspired fractions across the
            // node segment.
            const double pamb0 = a.pamb_atm + (b.pamb_atm - a.pamb_atm) * f0;
            const double pamb1 = a.pamb_atm + (b.pamb_atm - a.pamb_atm) * f1;
            const double fo2_0 = a.fo2 + (b.fo2 - a.fo2) * f0;
            const double fo2_1 = a.fo2 + (b.fo2 - a.fo2) * f1;
            const double fhe_0 = a.fhe + (b.fhe - a.fhe) * f0;
            const double fhe_1 = a.fhe + (b.fhe - a.fhe) * f1;
            const double fn2_0 = 1.0 - fo2_0 - fhe_0;
            const double fn2_1 = 1.0 - fo2_1 - fhe_1;

            const double br0 = lem_arterial_bracket(p, pamb0);
            const double br1 = lem_arterial_bracket(p, pamb1);
            const double pa_n2_0 = fn2_0 * br0, pa_n2_1 = fn2_1 * br1;
            const double pa_he_0 = fhe_0 * br0, pa_he_1 = fhe_1 * br1;
            const double pao2_0 = lem_alveolar_po2(p, pamb0, fn2_0, fhe_0);
            const double pao2_1 = lem_alveolar_po2(p, pamb1, fn2_1, fhe_1);

            // Hazard at step start (pre-advance state).
            const double h0 = hazard_at(pamb0, h0_cmpt);

            // Advance every compartment across the step.
            for (std::size_t i = 0; i < nc; ++i) {
                const LemCompartment& c = p.compartments[i];
                const double pa_o2_0 = std::max(0.0, pao2_0 - c.pset_ata);
                const double pa_o2_1 = std::max(0.0, pao2_1 - c.pset_ata);
                const double p_fix = p_fix_of(c);

                // Bubble-free dissolved advance (A9). Used when no bubble is
                // present, and after a bubble resolves within this step.
                auto advance_dissolved = [&]() {
                    tissue[i].n2 = lem_advance_tension(tissue[i].n2, pa_n2_0,
                                                       pa_n2_1, c.tc_n2_min, dt);
                    tissue[i].he = lem_advance_tension(tissue[i].he, pa_he_0,
                                                       pa_he_1, c.tc_he_min, dt);
                    tissue[i].o2 = lem_advance_tension(tissue[i].o2, pa_o2_0,
                                                       pa_o2_1, c.tc_o2_min, dt);
                };

                if (!bubble[i].active) {
                    advance_dissolved();
                    // Nucleation (A13): a bubble forms once the burden exceeds
                    // the EL bubble-formation pressure, i.e. SS_i > PXO_i.
                    if (p.enable_bubbles && c.pxo_atm < 1.0e9) {
                        const double A = pamb1 - p_fix + c.pxo_atm;
                        if (tissue[i].burden() > A) {
                            bubble[i].active = true;
                            bubble[i].bp_o2 = tissue[i].o2;
                            bubble[i].bp_n2 = tissue[i].n2;
                            bubble[i].bp_he = tissue[i].he;
                        }
                    }
                } else {
                    // Bubble present. Solve the constraint (A13/A22) for the
                    // bubble size X and the clamped dissolved tensions, then
                    // wash the burden out at the perfusion rate (A10).
                    const double A = pamb1 - p_fix + c.pxo_atm;
                    // Table 4's tissue solubility alpha_t is 0 for He and O2,
                    // which is undefined in P' = n/alpha_t. Treat He (and O2)
                    // with N2's tissue solubility (0.073) — i.e. He behaves the
                    // same as N2 in the bubble burden/solve.
                    const double a_t = c.at_n2;
                    const double alpha[3] = {a_t, a_t, a_t};
                    double Pp[3] = {bubble[i].bp_o2, bubble[i].bp_n2,
                                    bubble[i].bp_he};
                    if (Pp[0] + Pp[1] + Pp[2] <= A) {
                        // Bubble resolves: dissolved == burden at X=0.
                        bubble[i].active = false;
                        tissue[i].o2 = Pp[0];
                        tissue[i].n2 = Pp[1];
                        tissue[i].he = Pp[2];
                        advance_dissolved();
                    } else {
                        const double X = lem_bubble_solve_X(A, alpha, Pp, 3);
                        const double pt_o2 = alpha[0] * Pp[0] / (alpha[0] + X);
                        const double pt_n2 = alpha[1] * Pp[1] / (alpha[1] + X);
                        const double pt_he = alpha[2] * Pp[2] / (alpha[2] + X);
                        // A10: dP'_g/dt = (p_a,g - p_t,g)/tau_g (Euler step).
                        bubble[i].bp_o2 = std::max(
                            0.0, Pp[0] + dt * (pa_o2_1 - pt_o2) / c.tc_o2_min);
                        bubble[i].bp_n2 = std::max(
                            0.0, Pp[1] + dt * (pa_n2_1 - pt_n2) / c.tc_n2_min);
                        bubble[i].bp_he = std::max(
                            0.0, Pp[2] + dt * (pa_he_1 - pt_he) / c.tc_he_min);
                        // Record dissolved tensions for reporting.
                        tissue[i].o2 = pt_o2;
                        tissue[i].n2 = pt_n2;
                        tissue[i].he = pt_he;
                    }
                }
            }

            // Hazard at step end (post-advance state).
            const double h1 = hazard_at(pamb1, h1_cmpt);

            // Trapezoidal contribution to the risk integral (A26), per cmpt.
            for (std::size_t i = 0; i < nc; ++i) {
                score.compartment_hazard[i] +=
                    0.5 * (h0_cmpt[i] + h1_cmpt[i]) * dt;
                const LemCompartment& c = p.compartments[i];
                const double ss1 = burden_of(i) - (pamb1 - p_fix_of(c));
                if (ss1 > c.pxo_atm) score.bubble_regime_min[i] += dt;
            }
            score.hazard_integral += 0.5 * (h0 + h1) * dt;

            // Quiescent fast-forward (the report's analytic node-to-node A9
            // tracking during periods with no risk accumulation or bubble). On
            // a constant-conditions segment, if no bubble is active and even
            // the maximum possible future burden — each gas driven
            // monotonically to its arterial value, so bounded by
            // max(tissue, arterial) — stays below every risk (THR) and
            // nucleation (PXO) threshold, the rest of the segment contributes
            // zero risk and cannot nucleate. Advance the state analytically to
            // the segment end in one A9 step and stop fine-stepping. This is
            // exact, not an approximation.
            if (constant_seg) {
                bool skip = true;
                for (std::size_t i = 0; i < nc && skip; ++i) {
                    if (bubble[i].active) {
                        skip = false;
                        break;
                    }
                    const LemCompartment& c = p.compartments[i];
                    const double pa_o2 = std::max(0.0, pao2_1 - c.pset_ata);
                    const double fbmax = std::max(tissue[i].o2, pa_o2) +
                                         std::max(tissue[i].n2, pa_n2_1) +
                                         std::max(tissue[i].he, pa_he_1);
                    double thr = c.thr_atm;
                    if (p.enable_bubbles && c.pxo_atm < 1.0e9)
                        thr = std::min(thr, c.pxo_atm);
                    if (fbmax > pamb1 - p_fix_of(c) + thr) skip = false;
                }
                if (skip) {
                    const double remaining = seg_len * (1.0 - f1);
                    if (remaining > 0.0) {
                        for (std::size_t i = 0; i < nc; ++i) {
                            const LemCompartment& c = p.compartments[i];
                            const double pa_o2 =
                                std::max(0.0, pao2_1 - c.pset_ata);
                            tissue[i].n2 = lem_advance_tension(
                                tissue[i].n2, pa_n2_1, pa_n2_1, c.tc_n2_min,
                                remaining);
                            tissue[i].he = lem_advance_tension(
                                tissue[i].he, pa_he_1, pa_he_1, c.tc_he_min,
                                remaining);
                            tissue[i].o2 = lem_advance_tension(
                                tissue[i].o2, pa_o2, pa_o2, c.tc_o2_min,
                                remaining);
                        }
                    }
                    break;
                }
            }
        }
    }

    score.total_time_min = nodes.back().time_min - nodes.front().time_min;
    score.p_dcs = 1.0 - std::exp(-score.hazard_integral);
    return score;
}

// ---------------------------------------------------------------------------
// Schedule generation at a target risk
// ---------------------------------------------------------------------------
//
// The LEM model is probabilistic: unlike the deterministic EL-DCM engine it
// has no MPTT ceiling to ascend against, so a decompression schedule is found
// by SEARCH — score candidate ascents and add stop time where it most reduces
// P_DCS until the target is met. (TR 02-10 itself sidesteps this by mapping
// LEM onto a deterministic model to produce Navy-format tables, §2.2.1; this
// is instead native LEM-scored generation.)

// A dive to plan a decompression schedule for. Breathing is CCR (constant
// PO2) when `po2_setpoint` is set, otherwise open-circuit `oc_gas`. The same
// breathing mode is used on the bottom and throughout ascent — for CCR that
// is exactly the MK 16 constant-1.3-ata mode; open-circuit deco-gas switches
// are not modeled here yet.
struct LemDivePlan {
    double max_depth_fsw = 0.0;
    double bottom_time_min = 0.0;  // time held AT max depth (excludes descent)

    std::optional<double> po2_setpoint;  // CCR setpoint, atm
    double diluent_fhe = 0.0;            // CCR diluent He fraction (rest N2)
    GasMix oc_gas;                       // open-circuit inspired mix

    double descent_rate_fsw_per_min = 60.0;
    double ascent_rate_fsw_per_min = 30.0;

    double first_stop_fsw = 0.0;         // 0 => auto (deepest increment < max)
    double last_stop_fsw = 20.0;
    double stop_increment_fsw = 10.0;
};

struct LemStop {
    double depth_fsw = 0.0;
    double time_min = 0.0;
};

struct LemSchedule {
    std::vector<LemStop> stops;      // deepest-first, only stops with time > 0
    double p_dcs = 0.0;              // achieved P_DCS of the full schedule
    double decompression_time_min = 0.0;  // total ascent time (stops + travel)
    bool met_target = false;
    LemProfile profile;             // the full scored profile (for inspection)
};

// Build a dive-log profile from a plan and a per-candidate-stop time vector
// (indexed same as `stop_depths`, deepest-first). Applies the breathing mode.
inline LemProfile lem_build_profile(const LemDivePlan& plan,
                                    const std::vector<double>& stop_depths,
                                    const std::vector<double>& stop_times) {
    auto mk = [&](double t, double d) {
        LemNode n;
        n.time_min = t;
        n.depth_fsw = d;
        if (plan.po2_setpoint) {
            n.po2_setpoint = plan.po2_setpoint;
            n.diluent_fhe = plan.diluent_fhe;
        } else {
            n.gas = plan.oc_gas;
        }
        return n;
    };
    LemProfile p;
    double t = 0.0, d = 0.0;
    p.push_back(mk(t, d));
    // Descent.
    t += plan.max_depth_fsw / plan.descent_rate_fsw_per_min;
    d = plan.max_depth_fsw;
    p.push_back(mk(t, d));
    // Bottom.
    t += plan.bottom_time_min;
    p.push_back(mk(t, d));
    // Ascend through stops (deepest first).
    for (std::size_t i = 0; i < stop_depths.size(); ++i) {
        const double sd = stop_depths[i];
        if (sd >= d) continue;  // never descend during ascent
        t += (d - sd) / plan.ascent_rate_fsw_per_min;  // travel up
        d = sd;
        p.push_back(mk(t, d));
        if (stop_times[i] > 0.0) {
            t += stop_times[i];
            p.push_back(mk(t, d));  // hold
        }
    }
    // Final ascent to surface.
    if (d > 0.0) {
        t += d / plan.ascent_rate_fsw_per_min;
        p.push_back(mk(t, 0.0));
    }
    return p;
}

// Generate a decompression schedule whose LEM P_DCS meets `target_pdcs`.
// Greedy: starting from a direct ascent, repeatedly add one minute to the
// candidate stop whose addition most reduces P_DCS, until the target is met
// (or no stop helps / a total-deco cap is hit). Stop times come out in whole
// minutes, matching the Navy-table convention.
inline LemSchedule lem_generate_schedule(const LemParams& params,
                                         const LemDivePlan& plan,
                                         double target_pdcs,
                                         double max_deco_min = 600.0) {
    // Candidate stop depths, deepest first.
    double first = plan.first_stop_fsw;
    if (first <= 0.0) {
        first = std::floor((plan.max_depth_fsw - 1e-9) / plan.stop_increment_fsw) *
                plan.stop_increment_fsw;
    }
    std::vector<double> depths;
    for (double sd = first; sd >= plan.last_stop_fsw - 1e-9;
         sd -= plan.stop_increment_fsw) {
        if (sd < plan.max_depth_fsw) depths.push_back(sd);
    }
    std::vector<double> times(depths.size(), 0.0);

    auto score_times = [&](const std::vector<double>& tt) {
        return lem_score(params, lem_build_profile(plan, depths, tt)).p_dcs;
    };

    LemSchedule sched;
    double cur = score_times(times);  // direct ascent
    const double dt = 1.0;            // whole-minute increments

    while (cur > target_pdcs) {
        double total = 0.0;
        for (double x : times) total += x;
        if (total >= max_deco_min) break;

        int best = -1;
        double best_new = cur;
        for (std::size_t i = 0; i < depths.size(); ++i) {
            std::vector<double> trial = times;
            trial[i] += dt;
            double np = score_times(trial);
            if (np < best_new) {
                best_new = np;
                best = static_cast<int>(i);
            }
        }
        if (best < 0) break;  // no stop reduces risk further
        times[best] += dt;
        cur = best_new;
    }

    // Assemble result.
    sched.p_dcs = cur;
    sched.met_target = cur <= target_pdcs + 1e-12;
    sched.profile = lem_build_profile(plan, depths, times);
    for (std::size_t i = 0; i < depths.size(); ++i) {
        if (times[i] > 0.0) sched.stops.push_back({depths[i], times[i]});
    }
    if (!sched.profile.empty()) {
        // Decompression time = from leaving the bottom to surfacing.
        double t_bottom_end = plan.max_depth_fsw / plan.descent_rate_fsw_per_min +
                              plan.bottom_time_min;
        sched.decompression_time_min =
            sched.profile.back().time_min - t_bottom_end;
    }
    return sched;
}

}  // namespace deco
