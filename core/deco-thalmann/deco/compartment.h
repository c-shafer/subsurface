// libdeco/include/deco/compartment.h
//
// Perfusion-limited compartment state and EL-DCM kinetics. See profile.h
// for unit conventions (fsw throughout).
//
// Provenance (see DEVLOG.md for the full writeup): this is a port of
// NEDU TR 10-09 (Gerth, 2010)'s STIM7 backward stop-time solve (Figure
// 28), confirmed via a 600 DPI crop of the source flowchart (the
// original medium-resolution read had misjudged one digit — KSat vs
// KDsat — and one diamond's comparison direction; both are now
// pixel-confirmed, see DEVLOG's "STIM7 high-resolution correction"
// entry). Rate-constant rule, CONFIRMED at high resolution:
//   - Saturating (uptake, tension <= PAN2): always KSat = ln2/half_time.
//   - Desaturating (tension > PAN2): always KDsat = KSat*sdr, for BOTH
//     the linear phase (while tension > PVN2) and the exponential phase
//     that follows it (tension <= PVN2) — not a linear-vs-exponential
//     split as an earlier draft of this file assumed. Consequence: the
//     crossover is tangent-continuous (no dP/dt jump) regardless of
//     sdr, because both phases share the same rate constant.
//   - PS/branch selection (which of pure-linear / pure-exponential /
//     both-phases applies) is likewise pixel-confirmed.
//
// RAMPING AMBIENT (ascent/descent legs): advance_compartment_ramp()
// ports TR 10-09 Figures 33/34 for a linearly-changing ambient
// pressure, in BOTH CPO2 (CCR) and constant-fraction (open-circuit)
// modes. The two modes differ physically, not just by a flag: PAN2(t)
// (the uptake asymptote) always ramps at RINRT, but PVN2(t) (the
// crossover level) always ramps at RAMB, and RINRT == RAMB only under
// CPO2 (TR 10-09 Figure 31: RINRT=RAMB, RO2=0 for CPO2; RINRT=FN2*RAMB,
// RO2=RAMB-RINRT for constant-fraction). So under open-circuit the
// crossover *gap* PVN2(t)-PAN2(t) = gap0 + RO2*t genuinely widens or
// narrows over time — independently re-derived and confirmed this
// against Figure 35 ("Linear Update"), whose P(i) +=
// KDsat*(A*TLIN-B*TLIN^2) term (A=PAN2-PVN2, B=RO2/2) is exactly the
// integral of dP/dt=-KDsat*gap(t) with a time-varying gap. Figure 34's
// TXOVER (dropping out of the linear phase) becomes a genuine quadratic
// when RO2 != 0 — also re-derived independently and confirmed exact.
// See DEVLOG.md, "Open-circuit ramping support" entry, for the full
// derivation.
//
// Figure 33's reverse direction (rising into the linear phase from
// exponential) has NO closed form when RO2 != 0 — TR 10-09 says so
// explicitly ("TXOVER must be found by numerical iteration") and uses
// Newton-Raphson from a CPO2-style approximate starting guess. This
// file also uses Newton-Raphson against the same target equation
// (independently derived, see detail::time_to_linear_crossover_general)
// but does NOT attempt to replicate TR 10-09's exact starting-guess
// selection or its "T >= TI" short-circuit ordering between the two
// candidate formulas — the source's own flowchart connector routing
// for that specific bookkeeping could not be read with confidence even
// at 600 DPI (Figure 33, lower half), and unlike a misread comparison
// operator, guessing at numerical-method plumbing risks a genuinely
// different (if usually similar) answer, not just a differently-shaped
// but equivalent implementation. Deliberate engineering choice instead:
// solve the same equation with a damped, bounded Newton-Raphson and a
// residual-based convergence check, falling back to "no crossover
// detected" on non-convergence — safe by construction (bounded
// iterations, no exceptions in the numeric core) rather than a
// numerically-unverified port of an unclear source diagram.
//
// TNODE (TR 10-09 Appendix A: "time of minimum or maximum tissue inert
// gas tension in a profile sub-segment") IS handled, but by derivation
// rather than transcription — no formula box for it was found anywhere
// in the source, only the variable definition. Since dP/dt = -k*(P -
// PAN2(t)) holds regardless of ramping, a node is just the time at
// which the supersaturation-from-the-ramping-target Q(t) = P(t)-PAN2(t)
// returns to 0 — the same closed form as TXOVER, aimed at level 0
// instead of the crossover gap. See DEVLOG.md for the full derivation
// and the monotonicity argument that bounds how many transitions a
// single call can ever need (at most a handful).
//
// STRONGEST confirmation to date (2026-07-22 final review pass): TR
// 10-09 Figures 35/36 ("Linear Update"/"Exponential Update" — the
// actual routines UPDT7 calls to apply a TLIN/TEXP duration) were
// located and checked algebraically. Figure 36's
// P(i) = A*(exp(-K*TEXP)-1)+B, with A = P(i)-PAN2+RINRT/K and
// B = RINRT*TEXP+P(i), expands to exactly this file's ramping-
// exponential formula term for term (RINRT == this file's r, K == k).
// Figure 35's P(i) += KDsat*(A*TLIN - B*TLIN^2) with A=PAN2-PVN2,
// B=RO2/2 reduces to this file's b = -KDsat*gap linear slope exactly
// when RO2 == 0 (true under CPO2, our only supported ramping mode) —
// note the RO2 != 0 case is genuinely quadratic in TLIN, not linear
// in the literal sense; "Linear Update" names the kinetics *regime*,
// not the shape of P(t) in general. This is now the most direct
// evidence for this file's forward-update formulas: not just a
// cross-derivation match against the TXOVER *timing* formulas
// (Figures 33/34), but a match against the actual state-update
// formulas themselves.
//
// IMPORTANT CAVEAT on how this file relates to TR 10-09's control
// flow: advance_compartment() and advance_compartment_ramp() are
// NOT a transliteration of Figure 30 (UPDT7)'s specific GOTO-labeled
// branch structure (DESAT flag bookkeeping, the 200/300/400/450
// jump targets, etc.). They are an independently-constructed
// state machine built from the CONFIRMED closed-form pieces above
// (crossover times, node times, update formulas), verified to produce
// the same results via cross-checks (formula matches, round-trip
// tests, restart-composition tests) rather than by matching Figure
// 30's box-for-box shape. Behaviorally equivalent by construction and
// by test, not a line-by-line port — worth knowing if ever diffing
// this file against the source flowchart directly.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace deco {

// Inert-gas tension state for a single perfusion-limited compartment.
// A compartment tracks one inert gas at a time in the EL-DCM engine
// (single-inert-gas by design — see spec §10 pitfall 1).
struct CompartmentState {
    double tension_fsw = 0.0;
};

// Per-compartment kinetics parameters, constant for a given ParameterSet
// (see eldcm.h) — not part of the mutable CompartmentState.
struct CompartmentSpec {
    double half_time_min = 0.0;
    // SDR(i): asymmetric desaturation-rate ratio. KDsat = KSat * sdr
    // governs the entire desaturating (washout) regime; KSat alone
    // governs saturating (uptake). 1.0 = no asymmetry. [EXTRACT per
    // regime — TR 10-09 Figure 32; whether XVal-He-8_040/9_040 use
    // sdr != 1.0 is not yet known, pending TR 18-05.]
    double sdr = 1.0;
};

// Resolves the effective spec to feed advance_compartment()/
// advance_compartment_ramp()/required_stop_time() given the current
// gas-delivery mode — TR 10-09 Figure 32's CNDSDR_FO2 gate: SDR applies
// (KDsat = KSat*sdr) when constant_po2 is true, OR fn2 <= (1-cndsdr_fo2);
// otherwise KDsat is forced equal to KSat (sdr=1.0) regardless of the
// compartment's own configured sdr. Always applies under CPO2 mode
// (the "constant_po2 OR ..." short-circuits), so this is a no-op for
// the target He-O2 CCR regimes — needed for correctness once
// open-circuit/air ParameterSets with sdr != 1.0 are used.
inline CompartmentSpec resolve_compartment_spec(const CompartmentSpec& spec, bool constant_po2,
                                                 double fn2, double cndsdr_fo2) {
    const bool sdr_applies = constant_po2 || (fn2 <= (1.0 - cndsdr_fo2));
    CompartmentSpec resolved = spec;
    if (!sdr_applies) resolved.sdr = 1.0;
    return resolved;
}

// Ambient state driving a compartment's kinetics at a given instant,
// already resolved from depth/PO2-mode/physiological-offset inputs
// (TR 10-09 Figure 31, UPDT7 Initialize) into the two working
// quantities the kinetics formulas actually consume:
//   pan2_fsw: PAN2 = MAX(PAMB - (PAO2+PACO2+PH2O), 0) — the asymptote
//     for uptake.
//   pvn2_fsw: PVN2 = PAMB - (PVO2+PVCO2+PH2O) + LPBOVP — the tissue
//     tension level at which washout switches between linear and
//     exponential kinetics. Note pvn2 - pan2 is depth-independent (PAMB
//     cancels in the difference), so this gap is a fixed width even
//     though the absolute pan2/pvn2 levels move with depth.
struct AmbientState {
    double pan2_fsw = 0.0;
    double pvn2_fsw = 0.0;
};

// Advances a compartment's tension over a duration at constant ambient
// (fixed depth) — TR 10-09 UPDT7's DC=0 case.
inline void advance_compartment(CompartmentState& state,
                                 const AmbientState& amb,
                                 const CompartmentSpec& spec,
                                 double duration_min) {
    const double k_sat = std::log(2.0) / spec.half_time_min;
    const double k_dsat = k_sat * spec.sdr;
    const double p0 = state.tension_fsw;

    if (p0 <= amb.pan2_fsw) {
        // Saturating (uptake): always exponential at k_sat toward PAN2.
        state.tension_fsw =
            amb.pan2_fsw + (p0 - amb.pan2_fsw) * std::exp(-k_sat * duration_min);
        return;
    }

    // Desaturating: always at k_dsat, whether or not the linear phase
    // is engaged.
    const double gap = amb.pvn2_fsw - amb.pan2_fsw;
    if (gap <= 0.0 || p0 <= amb.pvn2_fsw) {
        // Already at/below the crossover level (or crossover disabled):
        // pure exponential washout at k_dsat toward PAN2.
        state.tension_fsw =
            amb.pan2_fsw + (p0 - amb.pan2_fsw) * std::exp(-k_dsat * duration_min);
        return;
    }

    // Starting above the crossover level PVN2: linear phase first, at
    // rate KDsat*(PVN2-PAN2) (TR 10-09 TLIN formula body), tangent-
    // continuous with the exponential phase that follows — both use
    // k_dsat, so there is no slope discontinuity at the crossover
    // regardless of sdr.
    const double b = -k_dsat * gap;
    const double t_cross = (p0 - amb.pvn2_fsw) / (k_dsat * gap);

    if (duration_min <= t_cross) {
        state.tension_fsw = p0 + b * duration_min;
        return;
    }

    const double remaining = duration_min - t_cross;
    state.tension_fsw = amb.pan2_fsw + gap * std::exp(-k_dsat * remaining);
}

namespace detail {

// Solves Q(t) = target_s for t > 0, where Q(t) = P(t)-PAN2(t) evolves
// as Q(t) = -r/k + (s0 + r/k)*exp(-k*t) — the shared closed form behind
// both TXOVER (target_s = the crossover gap) and TNODE (target_s = 0).
// Q(t) is a single monotonic exponential approaching -r/k, so it can
// reach a given target_s at most once, in the future, iff target_s and
// s0 lie on the same side of -r/k and target_s is strictly closer to it
// — equivalently, the ratio computed below lies in the open interval
// (0, 1). Returns +infinity ("no such time") otherwise, including the
// degenerate r == 0 case (a non-ramping target can't newly reach
// anything it hasn't already reached).
inline double time_to_supersaturation_level(double k, double s0, double r, double target_s) {
    const double inf = std::numeric_limits<double>::infinity();
    const double asym_offset = r / k;
    const double den = s0 + asym_offset;
    if (den == 0.0) return inf;
    const double ratio = (target_s + asym_offset) / den;
    if (!(ratio > 0.0 && ratio < 1.0)) return inf;
    return -(1.0 / k) * std::log(ratio);
}

// TXOVER, linear -> exponential direction (TR 10-09 Figure 34): the
// linear phase's tension P(t) = p0 - k*gap0*t - k*ro2/2*t^2 (Figure 35,
// integral of dP/dt=-k*gap(t)) meets the ramping crossover level
// PVN2(t) = pan2_0+gap0 + (rinrt+ro2)*t. Exact closed form when
// ro2 == 0; a genuine quadratic (A=ro2*k/2, B=ramb+k*gap0, C=gap0-s0)
// otherwise — both forms independently re-derived and confirmed to
// match Figure 34's A/B/C/D exactly (see DEVLOG.md). Guard (pixel-
// confirmed): D<0 or B<=0 means no crossover.
inline double time_to_exponential_crossover(double k, double s0, double rinrt, double ro2,
                                             double gap0) {
    const double inf = std::numeric_limits<double>::infinity();
    const double ramb = rinrt + ro2;
    const double B = ramb + k * gap0;
    const double C = gap0 - s0;
    if (ro2 == 0.0) {
        if (B <= 0.0) return inf;
        const double t = -(C / B);
        return (t > 1e-12) ? t : inf;
    }
    const double A = ro2 * k / 2.0;
    const double D = B * B - 4.0 * A * C;
    if (D < 0.0 || B <= 0.0) return inf;
    const double t = (-B + std::sqrt(D)) / (2.0 * A);
    return (t > 1e-12) ? t : inf;
}

// TXOVER, exponential -> linear direction (TR 10-09 Figure 33): no
// closed form when ro2 != 0 (Q(t), a single exponential, is set equal
// to a linearly-moving target gap0+ro2*t — transcendental). Solved via
// a damped, bounded Newton-Raphson against
// f(t) = Q(t) - (gap0+ro2*t), f'(t) = -k*(s0+rinrt/k)*exp(-k*t) - ro2
// (both independently derived, see DEVLOG.md and the header comment on
// why this does not attempt to replicate TR 10-09's exact
// starting-guess/short-circuit bookkeeping for this step). Falls back
// to +infinity ("no crossover detected") on non-convergence rather
// than guessing.
inline double time_to_linear_crossover(double k, double s0, double rinrt, double ro2,
                                        double gap0) {
    const double inf = std::numeric_limits<double>::infinity();
    if (ro2 == 0.0) {
        return time_to_supersaturation_level(k, s0, rinrt, gap0);
    }
    const double asym = rinrt / k;
    auto f = [&](double t) { return (-asym + (s0 + asym) * std::exp(-k * t)) - (gap0 + ro2 * t); };
    auto fprime = [&](double t) { return -k * (s0 + asym) * std::exp(-k * t) - ro2; };

    double t = time_to_supersaturation_level(k, s0, rinrt, gap0);  // ro2=0 approximation as a start
    if (!std::isfinite(t) || t <= 0.0) t = 0.0;

    bool converged = false;
    for (int i = 0; i < 50; ++i) {
        const double fi = f(t);
        if (std::fabs(fi) < 1e-9) {
            converged = true;
            break;
        }
        const double fpi = fprime(t);
        if (fpi == 0.0) break;
        double t_new = t - fi / fpi;
        if (!std::isfinite(t_new) || t_new < 0.0) t_new = t / 2.0;  // damped fallback
        t = t_new;
    }
    if (!converged && std::fabs(f(t)) > 1e-6) return inf;
    return (t > 1e-9) ? t : inf;
}

}  // namespace detail

// A compartment's classification within a single ramping-ambient call.
// Tracked explicitly rather than re-derived from tension vs. PAN2/PVN2
// after each transition: both TXOVER directions land exactly on
// tension == PVN2(t), so position alone is ambiguous right at that
// boundary — only the transition that produced it disambiguates which
// regime comes next (see DEVLOG.md, "TNODE implementation" entry).
enum class Regime { kSaturating, kExpDesat, kLinearDesat };

// Advances a compartment's tension over duration_min during which
// ambient is ramping linearly (an ascent/descent leg), in either CPO2
// or constant-fraction mode. rinrt_fsw_per_min is PAN2(t)'s ramp rate;
// ro2_fsw_per_min is the crossover gap's rate of change (PVN2(t)'s
// ramp rate minus rinrt). Pass ro2_fsw_per_min = 0.0 for CPO2 mode
// (where the gap is constant); for constant-fraction mode, the caller
// computes rinrt = FN2*RAMB and ro2 = RAMB-rinrt from TR 10-09 Figure
// 31 — this function itself is mode-agnostic, consuming only the two
// resolved rates. amb0 gives PAN2/PVN2 at the start of the interval.
//
// Handles all regime transitions a ramping ambient can cause within
// the interval: TNODE (saturating <-> desaturating flip) and TXOVER in
// either direction (crossing into/out of the linear phase, including
// the genuinely time-varying crossover gap when ro2 != 0 — see header
// comment). Each transition strictly advances the clock; by the
// monotonicity of the single-exponential Q(t) (see
// detail::time_to_supersaturation_level), a freshly-entered exponential
// regime cannot immediately repeat the transition that produced it —
// so the loop below is bounded by a small, fixed number of regime
// changes for the exponential regimes. The linear regime under ro2 !=
// 0 does not have the same monotonicity guarantee in general (the
// crossover-detection there is a quadratic, which can have two roots),
// but TXOVER out of the linear phase is still searched fresh each
// entry, so this remains correct — just not backed by as clean a
// termination argument; the iteration guard below is the backstop.
inline void advance_compartment_ramp(CompartmentState& state, const AmbientState& amb0,
                                      double rinrt_fsw_per_min, double ro2_fsw_per_min,
                                      const CompartmentSpec& spec, double duration_min) {
    const double k_sat = std::log(2.0) / spec.half_time_min;
    const double k_dsat = k_sat * spec.sdr;
    const double rinrt = rinrt_fsw_per_min;
    const double ro2 = ro2_fsw_per_min;
    const double inf = std::numeric_limits<double>::infinity();

    double p = state.tension_fsw;
    double pan2 = amb0.pan2_fsw;              // PAN2 at the start of the CURRENT sub-segment
    double gap = amb0.pvn2_fsw - amb0.pan2_fsw;  // gap AT the current sub-segment's start
    double t_left = duration_min;

    Regime regime;
    if (p <= pan2) {
        regime = Regime::kSaturating;
    } else if (gap <= 0.0 || p <= pan2 + gap) {
        regime = Regime::kExpDesat;
    } else {
        regime = Regime::kLinearDesat;
    }

    for (int guard = 0; guard < 32; ++guard) {
        const double s0 = p - pan2;

        if (regime == Regime::kSaturating) {
            // Chase the ramping target at k_sat; the only possible
            // event is a node (target overtakes it -> desaturating).
            // TNODE's target (P == PAN2(t)) is time-invariant, so this
            // stays an exact closed form regardless of ro2.
            const double t_node = detail::time_to_supersaturation_level(k_sat, s0, rinrt, 0.0);
            if (t_node < t_left) {
                p = pan2 + rinrt * t_node;  // == PAN2(t_node) exactly, S == 0 there
                pan2 += rinrt * t_node;
                gap += ro2 * t_node;
                t_left -= t_node;
                regime = Regime::kExpDesat;  // always: S==0 is always <= gap when gap > 0
                continue;
            }
            const double q = -rinrt / k_sat + (s0 + rinrt / k_sat) * std::exp(-k_sat * t_left);
            state.tension_fsw = pan2 + rinrt * t_left + q;
            return;
        }

        if (regime == Regime::kExpDesat) {
            // Chase the ramping target at k_dsat. Two possible next
            // events — a node (flips to saturating) or a rise into the
            // linear regime (TR 10-09 Figure 33) — take whichever
            // comes first. The rise-into-linear search accounts for a
            // moving gap when ro2 != 0 (Newton-Raphson, see
            // detail::time_to_linear_crossover).
            const double t_node = detail::time_to_supersaturation_level(k_dsat, s0, rinrt, 0.0);
            const double t_xover = (gap > 0.0) ? detail::time_to_linear_crossover(
                                                      k_dsat, s0, rinrt, ro2, gap)
                                                : inf;
            const double t_next = std::min(t_node, t_xover);
            if (t_next < t_left) {
                const bool via_node = (t_next == t_node);
                const double gap_at_next = gap + ro2 * t_next;
                p = pan2 + rinrt * t_next + (via_node ? 0.0 : gap_at_next);
                pan2 += rinrt * t_next;
                gap = gap_at_next;
                t_left -= t_next;
                regime = via_node ? Regime::kSaturating : Regime::kLinearDesat;
                continue;
            }
            const double q = -rinrt / k_dsat + (s0 + rinrt / k_dsat) * std::exp(-k_dsat * t_left);
            state.tension_fsw = pan2 + rinrt * t_left + q;
            return;
        }

        // kLinearDesat: P(t) = p - k_dsat*gap*t - k_dsat*ro2/2*t^2 (TR
        // 10-09 Figure 35, exact when ro2 == 0, a genuine quadratic
        // otherwise — see header comment). No interior extremum from
        // this formula alone when ro2 == 0 (constant slope); when
        // ro2 != 0 the only next event searched is still dropping back
        // into the exponential regime (Figure 34), found via
        // detail::time_to_exponential_crossover.
        const double t_cross = detail::time_to_exponential_crossover(k_dsat, s0, rinrt, ro2, gap);

        if (t_cross >= t_left) {
            state.tension_fsw = p - k_dsat * gap * t_left - k_dsat * ro2 / 2.0 * t_left * t_left;
            return;
        }

        const double gap_at_cross = gap + ro2 * t_cross;
        p = pan2 + rinrt * t_cross + gap_at_cross;
        pan2 += rinrt * t_cross;
        gap = gap_at_cross;
        t_left -= t_cross;
        regime = Regime::kExpDesat;
    }

    // Not expected to be reached for any physically realistic interval
    // (see the monotonicity argument above) — a defensive fallback
    // rather than an infinite loop or, worse, silently dropping
    // t_left. Applies the plain (non-transitioning) formula for
    // whichever regime currently applies, covering the rest of t_left
    // in one shot rather than continuing to search for transitions.
    const double s0 = p - pan2;
    if (regime == Regime::kSaturating) {
        const double q = -rinrt / k_sat + (s0 + rinrt / k_sat) * std::exp(-k_sat * t_left);
        state.tension_fsw = pan2 + rinrt * t_left + q;
    } else if (regime == Regime::kExpDesat) {
        const double q = -rinrt / k_dsat + (s0 + rinrt / k_dsat) * std::exp(-k_dsat * t_left);
        state.tension_fsw = pan2 + rinrt * t_left + q;
    } else {
        state.tension_fsw = p - k_dsat * gap * t_left - k_dsat * ro2 / 2.0 * t_left * t_left;
    }
}

// Computes the required hold time at the given ambient state before a
// compartment with current tension current_tension_fsw would satisfy
// target_tension_fsw (an MPTT ceiling for the next shallower stop) — the
// STIM7 backward solve (TR 10-09 Figure 28). Returns +infinity when no
// finite time satisfies the constraint: either the compartment is
// already past the target while still saturating (can never come back
// down by waiting at this depth), or its own equilibrium (PAN2) already
// exceeds the target (will never desaturate below it here).
inline double required_stop_time(double current_tension_fsw,
                                  const AmbientState& amb,
                                  const CompartmentSpec& spec,
                                  double target_tension_fsw) {
    const double k_sat = std::log(2.0) / spec.half_time_min;
    const double k_dsat = k_sat * spec.sdr;
    const double inf = std::numeric_limits<double>::infinity();

    if (current_tension_fsw <= amb.pan2_fsw) {
        // Saturating: how long until this compartment's rising tension
        // would first reach target_tension_fsw? Always k_sat.
        if (current_tension_fsw >= target_tension_fsw ||
            amb.pan2_fsw <= target_tension_fsw) {
            return inf;
        }
        const double t =
            (1.0 / k_sat) *
            std::log((current_tension_fsw - amb.pan2_fsw) /
                     (target_tension_fsw - amb.pan2_fsw));
        return std::max(t, 0.0);
    }

    // Desaturating: always k_dsat, for both TLIN and TEXP.
    if (current_tension_fsw <= target_tension_fsw) {
        return 0.0;
    }
    if (amb.pan2_fsw >= target_tension_fsw) {
        return inf;
    }

    const double gap = amb.pvn2_fsw - amb.pan2_fsw;
    double ps;
    double t_lin;
    double t_exp;
    if (gap <= 0.0 || current_tension_fsw <= amb.pvn2_fsw) {
        // No linear phase: already at/below the crossover level.
        ps = current_tension_fsw;
        t_lin = 0.0;
        t_exp = (1.0 / k_dsat) *
                std::log((ps - amb.pan2_fsw) / (target_tension_fsw - amb.pan2_fsw));
    } else if (target_tension_fsw >= amb.pvn2_fsw) {
        // Target itself is still above the crossover level: reached
        // entirely within the linear phase.
        ps = target_tension_fsw;
        t_lin = (current_tension_fsw - ps) / (k_dsat * gap);
        t_exp = 0.0;
    } else {
        // Both phases: linear down to PVN2, then exponential to target.
        ps = amb.pvn2_fsw;
        t_lin = (current_tension_fsw - ps) / (k_dsat * gap);
        t_exp = (1.0 / k_dsat) *
                std::log((ps - amb.pan2_fsw) / (target_tension_fsw - amb.pan2_fsw));
    }

    // TR 10-09 caps T at 9999 minutes ("T arbitrarily limited to 9999
    // minute maximum", Figure 28) — preserved for fidelity, though it
    // should never bind for realistic dive profiles.
    return std::min(t_lin + t_exp, 9999.0);
}

}  // namespace deco
