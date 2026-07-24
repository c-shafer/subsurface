// libdeco/include/deco/eldcm.h
//
// Thalmann Exponential-Linear Decompression Model (deterministic).
// Compartment kinetics (Phase 1) live in compartment.h. This file adds
// the "glue" connecting a ParameterSet to those kinetics (TR 10-09
// Figure 31) plus the MPTT/ceiling primitives (Figures 37, 28's
// multi-compartment aggregation). Greedy schedule generation itself
// (walking a profile stop by stop) is not implemented here yet. See
// implementation spec §4.1.
//
// ParameterSet's fields are transcribed from NEDU TR 10-09 Table 1
// ("Parameters input by Read_ini routine during TBLP7R startup") — see
// compartment.h's header comment and DEVLOG.md for what's confirmed vs.
// reconstructed. Numeric contents (every field marked [EXTRACT]) must be
// transcribed from the cited NEDU sources (TR 18-05, TR 07-09, ...) with
// a page/table citation in the loading code. Never fabricate these
// values — see spec §10 pitfall 7.
//
// MPTT row-offset convention (TR 10-09 p.11, "SetM routine"), CONFIRMED
// and load-bearing for both mptt_value() and find_ceiling_fsw() below:
// "the MPTTs for the... compartments at depth D, M[i,D], are offset by
// one stop depth increment (DINC) to depth D+DINC, because it is AT
// THIS DEEPER DEPTH that the M[i,D] are used to assess when ascent to
// depth D will be allowed. Thus, for DINC=10 fsw, the surfacing MPTT
// values (M0) appear in the 10 fsw row." I.e. the constraint that
// governs "can I ascend from X to X-DINC" is stored/consulted at row
// X (the CURRENT, deeper depth) — not at the target. Confirmed
// independently via TR 18-05: Table 12's M0 values match Appendix A's
// 10-fsw row exactly for both XVal-He-8_040 and XVal-He-9_040.
// mptt_value()'s contract follows this directly: pass the CURRENT
// (deeper) depth, get back the constraint for ascending one increment
// shallower. Workman's Eq. (2) form is analytic and already anchored
// at logical depth 0 (workman_mptt(c, 0) == M0 by construction), so it
// needs an explicit -DINC shift to match this same "pass current depth"
// contract — seemingly a special case, but it's what keeps the two
// MPTT representations behaviorally interchangeable.
//
// SetM (same source paragraph): when last_stop_depth_fsw isn't the
// table's own first row (stop_depth_increment_fsw) — true for
// XVal-He-8_040/9_040, last stop 20 fsw vs. first row 10 fsw (TR 18-05
// p.A-4) — the row at last_stop_depth_fsw must be swapped for the
// surfacing values (M0), since ascending from the last stop always
// targets the surface, not the table's next row up. Implemented as a
// special case inside mptt_value() (see below) rather than a table
// mutation, since Workman's analytic form has no discrete row to swap.
//
// Confidence note on find_ceiling_fsw() (TR 10-09 Figure 37, "Update
// IAD"): the CHAIN structure below (start deep, check the current
// row, step shallower while safe) is derived from the confirmed SetM
// paragraph above, not from Figure 37's FORTRAN DO-loop, whose exact
// early-exit/index-retention behavior could not be determined with
// confidence from the flowchart — a control-flow ambiguity in the
// same spirit as the Figure 33 Newton-Raphson bookkeeping question
// (see compartment.h and DEVLOG.md), not a formula transcription. Both
// this function's *predecessor* (a shallow-to-deep scan, since fixed —
// see DEVLOG.md's "MPTT row-offset correction" entry) and the current
// version should be re-checked once a schedule can be run end-to-end
// against a real golden table.
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "deco/compartment.h"
#include "deco/profile.h"

namespace deco {

// A single (depth, permissible-tension) control point in a compartment's
// MPTT / M-value table, both in fsw. Populated in Phase 2 from the
// source parameter report — see ParameterSet::name for provenance. Use
// this for a regime whose MPTT table is NOT well-described by Workman's
// linear form below (TR 10-09 explicitly supports arbitrary tables:
// "may not be built from solutions to simple analytic functions of
// stop depth").
struct MpttPoint {
    double depth_fsw = 0.0;
    double permissible_tension_fsw = 0.0;
};

struct MpttTable {
    // One control-point series per compartment, indexed the same as
    // ParameterSet::compartments.
    std::vector<std::vector<MpttPoint>> per_compartment;
};

// Workman's linear M-value form (TR 10-09 Eq. (2)): M_i(D) = M0_i +
// a_i*D — a surfacing M-value and depth-slope per compartment. Prefer
// this over MpttTable when the source regime's appendix gives
// (M0, slope) pairs directly rather than a full matrix — see
// DEVLOG.md's "Useful for Phase 2 feasibility" note.
struct WorkmanCompartment {
    double m0_fsw = 0.0;
    double slope = 0.0;  // unitless: fsw of permissible tension per fsw of depth
};

inline double workman_mptt(const WorkmanCompartment& c, double depth_fsw) {
    return c.m0_fsw + c.slope * depth_fsw;
}

// Plain-data description of one gas/PO2 regime (e.g. "XVal-He-8_040").
// Loading a different regime means loading different data, never
// recompiling logic — see spec §4.1. Field-by-field provenance is TR
// 10-09 Table 1 (see compartment.h) unless noted otherwise.
struct ParameterSet {
    std::string name;

    // Gas delivery mode (TR 10-09 CPO2 flag). He-O2 1.3 atm regimes
    // (XVal-He-8_040/9_040) are constant-PO2 (CCR); VVal-18/VVal-79
    // (air) are constant-fraction (open-circuit).
    bool constant_po2 = true;
    double po2_setpoint_atm = 0.0;  // used only if constant_po2 — see profile.h
    double fn2 = 0.0;               // used only if !constant_po2 (open-circuit)

    // Physiological offsets, fsw — [EXTRACT per regime].
    double paco2_fsw = 0.0;   // arterial CO2 partial pressure
    double ph2o_fsw = 0.0;    // tissue water vapor pressure
    double pvco2_fsw = 0.0;   // venous CO2 partial pressure
    double pvo2_fsw = 0.0;    // venous O2 partial pressure
    double ambao2_fsw = 0.0;  // ambient-arterial O2 partial pressure gradient

    // Crossover thresholds, fsw — [EXTRACT per regime]. TR 10-09 uses a
    // separate value at the surface (sPBOVP) vs. at depth (PBOVP);
    // STIM7/UPDT7 select LPBOVP = sPBOVP when CDepth == 0, else PBOVP.
    double pbovp_fsw = 0.0;
    double s_pbovp_fsw = 0.0;

    // FiO2 threshold gating SDR in open-circuit mode (TR 10-09 Figure
    // 32): SDR only applies when constant_po2 is true, OR fn2 <=
    // (1 - cndsdr_fo2). Irrelevant when constant_po2 is true (SDR
    // always applies there regardless of this value).
    double cndsdr_fo2 = 0.0;

    std::vector<CompartmentSpec> compartments;  // half_time_min + sdr, [EXTRACT]

    // MPTT representation — populate whichever fits the source regime
    // (see WorkmanCompartment/MpttTable comments above). workman takes
    // precedence when non-empty in the helpers below.
    std::vector<WorkmanCompartment> workman;  // [EXTRACT]
    MpttTable mptt;                           // [EXTRACT]

    // SURFP_FSW: surface pressure expressed as an fsw-equivalent, used
    // to convert depth (fsw) to absolute ambient pressure and to convert
    // an atm-denoted PO2 setpoint to fsw. [EXTRACT: confirm NEDU's
    // exact value — commonly 33 fsw/atm for seawater, not yet verified
    // against a source.]
    double surface_pressure_fsw = 33.0;

    double last_stop_depth_fsw = 20.0;
    double stop_depth_increment_fsw = 10.0;  // DINC — [EXTRACT: confirm]

    // RNDUPD (TR 10-09 Figure 16, "Round Time Up"): when true, each
    // computed stop time is rounded UP to the next whole minute before
    // it is applied. This is not merely output formatting — TR 10-09's
    // own note on Figure 16 states the rounded time is what feeds the
    // subsequent UPDT7 state update (with TTIS=F / NOT FRSTOP, which is
    // this library's convention), so it changes downstream tissue
    // loading, not just the printed value. TR 18-05 p.A-13 lists
    // RNDUPD=T among the switches used to generate its 1.3 atm PO2 He-O2
    // tables (Table 14 included), and whole-minute stop times are the
    // near-universal U.S. Navy table convention — so this defaults true.
    // Set it false only to inspect exact real-valued holds (e.g. for
    // debugging or a comparison that needs sub-minute resolution).
    bool round_stop_time_up = true;

    // O2-Air Cycling (TR 10-09 Figure 13, p.20) — the automatic
    // in-water O2 decompression procedure: once ascent reaches
    // o2ceil_fsw, switch from the bottom/travel gas to O2
    // (FN2 = 1 - o2time_fo2), then cycle o2time_min minutes on O2
    // against airtime_min minutes on air, subject to a one-time
    // gswlat_min gas-switch latency. Disabled by default
    // (o2_cycling_enabled = false) — every regime that doesn't specify
    // an OD/IW procedure is unaffected. All fields [EXTRACT per
    // regime]; VVal-79 (TR 12-01 Table B.2) is the first populated
    // example — see params/vval_79.h.
    bool o2_cycling_enabled = false;
    double o2ceil_fsw = 0.0;     // O2CEIL: max depth O2 breathing can start
    double o2time_min = 0.0;     // O2TIME: minutes on O2 per cycle
    double airtime_min = 0.0;    // AIRTIME: minutes on air per cycle
    double gswlat_min = 0.0;     // GSWLAT: air-to-O2 gas switch latency
    double o2time_fo2 = 0.0;     // O2TIME_FO2: inspired FO2 while ONO2 (FN2 = 1 - this)
    bool gs_dead = false;        // GS_DEAD: gas-switch latency is decompression dead time
    bool ab_dead = false;        // AB_DEAD: air-break time is decompression dead time
};

// Tissue tension of a diver saturated on AIR at the surface, in fsw —
// the documented default initial condition for a profile (TR 10-09
// Appendix B p.B-1: "the profile is started from initial saturation on
// air at the pressure D1 specified in this record"). Computed through
// the model's OWN arterial-tension formula (TR 10-09 Figure 31, the
// open-circuit / constant-fraction branch at FN2 = 0.79, depth 0), not
// as a bare 0.79*surface_pressure_fsw: the tissue equilibrates toward
// arterial inert tension, which carries this regime's PACO2/PH2O/AMBAO2
// offsets, so it sits ~PACO2 below the naive inspired-fraction product.
// Independent of the dive's own gas mode (CPO2 or open-circuit) — it
// models the diver's pre-dive state breathing air topside.
inline double air_saturation_tension_fsw(const ParameterSet& params) {
    constexpr double kAirN2Fraction = 0.79;
    const double pamb = params.surface_pressure_fsw;
    const double pao2 = (pamb - params.ph2o_fsw) * (1.0 - kAirN2Fraction) - params.ambao2_fsw;
    return std::max(pamb - (pao2 + params.paco2_fsw + params.ph2o_fsw), 0.0);
}

// Ambient pressure at a given depth. CF (TR 10-09's depth-unit
// conversion factor) is always 1.0 in this library's fsw-throughout
// convention (see profile.h) — TR 10-09 supports msw too via CF, not
// needed here.
inline double ambient_pressure_fsw(const ParameterSet& params, double depth_fsw) {
    return depth_fsw + params.surface_pressure_fsw;
}

// Applies the CNDSDR_FO2 gate (resolve_compartment_spec(), TR 10-09
// Figure 32) to every compartment in params, once, and returns a copy
// of params with the resolved compartments substituted in. constant_po2
// and fn2 are fixed for a given ParameterSet (they don't vary mid-dive
// in this model), so this only ever needs to run once per
// generate_schedule() call rather than per kinetics call — a no-op for
// every constant_po2 = true regime (CNDSDR_FO2's OR short-circuits),
// and load-bearing for open-circuit regimes with sdr != 1.0 (e.g.
// VVal-79's uniform 0.70 — see params/vval_79.h), where a straight-air
// dive (fn2 = 0.79 > 1 - cndsdr_fo2 = 0.20) forces sdr back to 1.0.
inline ParameterSet resolve_open_circuit_compartments(const ParameterSet& params) {
    ParameterSet resolved = params;
    for (auto& c : resolved.compartments) {
        c = resolve_compartment_spec(c, params.constant_po2, params.fn2, params.cndsdr_fo2);
    }
    return resolved;
}

// Resolves PAN2/PVN2 at a given depth — TR 10-09 Figure 31 (UPDT7
// Initialize) + Figure 30's PAN2/PVSAT/PVN2 formulas, pixel-confirmed
// (see DEVLOG.md). dc_fsw is the depth CHANGE for the call this state
// is being resolved for (0.0 for a fixed-depth stop, matching STIM7's
// own DC=0 case — the default); pass the actual signed depth change
// only when resolving the START of a ramping leg that might begin
// exactly at the surface, since Figure 31 selects PBOVP based on
// "CDEPTH <= 0 AND DC <= 0", not depth alone.
inline AmbientState resolve_ambient_state(const ParameterSet& params, double depth_fsw,
                                           double dc_fsw = 0.0) {
    const double pamb = ambient_pressure_fsw(params, depth_fsw);
    double pao2;
    if (params.constant_po2) {
        pao2 = params.po2_setpoint_atm * params.surface_pressure_fsw *
                   (1.0 - params.ph2o_fsw / pamb) -
               params.ambao2_fsw;
    } else {
        pao2 = (pamb - params.ph2o_fsw) * (1.0 - params.fn2) - params.ambao2_fsw;
    }
    const double pan2 = std::max(pamb - (pao2 + params.paco2_fsw + params.ph2o_fsw), 0.0);
    const double pvsat = pamb - (params.pvo2_fsw + params.pvco2_fsw + params.ph2o_fsw);
    const bool at_surface = (depth_fsw <= 0.0) && (dc_fsw <= 0.0);
    const double pbovp = at_surface ? params.s_pbovp_fsw : params.pbovp_fsw;
    return AmbientState{pan2, pvsat + pbovp};
}

// PAN2(t)/PVN2(t)'s ramp rates for an ascent/descent leg with ambient
// changing at ramb_fsw_per_min (signed: negative ascending, positive
// descending — CF == 1.0 in this library's convention, so this is
// directly the leg's depth rate). TR 10-09 Figure 31, pixel-confirmed.
struct RampRates {
    double rinrt_fsw_per_min = 0.0;
    double ro2_fsw_per_min = 0.0;
};

inline RampRates resolve_ramp_rates(const ParameterSet& params, double ramb_fsw_per_min) {
    if (params.constant_po2) {
        return RampRates{ramb_fsw_per_min, 0.0};
    }
    const double rinrt = params.fn2 * ramb_fsw_per_min;
    return RampRates{rinrt, ramb_fsw_per_min - rinrt};
}

// The MPTT constraint governing ascent from current_depth_fsw to
// current_depth_fsw - stop_depth_increment_fsw, for compartment
// compartment_index — i.e. pass the CURRENT (deeper) depth, not the
// target; see this file's header comment for why. Uses whichever MPTT
// representation the ParameterSet carries (Workman takes precedence
// when populated), and applies the SetM last-stop special case
// (ascending from last_stop_depth_fsw always targets the surface, so
// the surfacing MPTT M0 applies directly regardless of the table's
// next row up).
inline double mptt_value(const ParameterSet& params, std::size_t compartment_index,
                          double current_depth_fsw) {
    if (current_depth_fsw == params.last_stop_depth_fsw) {
        // SetM: ascending from the last stop always targets the
        // surface. The table's own row at stop_depth_increment_fsw IS
        // the surfacing value (M0) by the offset convention, so
        // recurse through the same special-case-free path using that
        // row — correct for both MPTT representations.
        return mptt_value(params, compartment_index, params.stop_depth_increment_fsw);
    }
    if (!params.workman.empty()) {
        // Workman's Eq. (2) is anchored at logical depth 0 directly
        // (workman_mptt(c, 0) == M0), unlike the table below — shift by
        // -DINC to match this function's "pass the current depth" contract.
        return workman_mptt(params.workman[compartment_index],
                             current_depth_fsw - params.stop_depth_increment_fsw);
    }
    const auto& points = params.mptt.per_compartment[compartment_index];
    // Linear interpolation over the control points (labeled by their
    // own table row depth, per the offset convention); clamps at the ends.
    if (current_depth_fsw <= points.front().depth_fsw)
        return points.front().permissible_tension_fsw;
    if (current_depth_fsw >= points.back().depth_fsw)
        return points.back().permissible_tension_fsw;
    for (std::size_t k = 1; k < points.size(); ++k) {
        if (current_depth_fsw <= points[k].depth_fsw) {
            const auto& a = points[k - 1];
            const auto& b = points[k];
            const double frac = (current_depth_fsw - a.depth_fsw) / (b.depth_fsw - a.depth_fsw);
            return a.permissible_tension_fsw +
                   frac * (b.permissible_tension_fsw - a.permissible_tension_fsw);
        }
    }
    return points.back().permissible_tension_fsw;  // unreachable given the clamps above
}

// The shallowest depth (an integer multiple of stop_depth_increment_fsw,
// at or above last_stop_depth_fsw) to which an instantaneous ascent is
// safe for every compartment — TR 10-09 Figure 37 ("Update IAD"). Chains
// upward from max_depth_fsw: at each depth, mptt_value(..., depth) tells
// us whether stepping to depth-DINC is safe (see this file's header
// comment for the row-offset convention this relies on); stop at the
// first unsafe step, or at last_stop_depth_fsw. tensions_fsw is indexed
// the same as params.compartments.
inline double find_ceiling_fsw(const ParameterSet& params,
                                const std::vector<double>& tensions_fsw, double max_depth_fsw) {
    const double dinc = params.stop_depth_increment_fsw;
    double current = max_depth_fsw;
    while (current > params.last_stop_depth_fsw + 1e-9) {
        bool safe = true;
        for (std::size_t i = 0; i < tensions_fsw.size(); ++i) {
            if (tensions_fsw[i] > mptt_value(params, i, current)) {
                safe = false;
                break;
            }
        }
        if (!safe) break;
        current -= dinc;
    }
    return current;
}

// TR 10-09 Figure 18 (Ascendr routine) — a REAL PORT, not a reconstruction:
// the flowchart was fully legible at 300 DPI (rendered from sources/ADA549883.pdf,
// PDF page 30), unlike Figure 37/29/33's ambiguous regions elsewhere in this
// codebase. Simulates a hypothetical ascent from current_depth_fsw to
// target_depth_fsw and returns the resulting tensions plus the ceiling ("IAD" —
// instantaneous ascent depth) implied by them. Operates on a COPY of
// entering_tensions and never mutates it — TR 10-09's FRSP7 (below) explicitly
// "restores entering tissue tensions" after each trial Ascendr call; this
// function's signature makes that automatic rather than requiring an explicit
// undo step.
//
// LST_DOMode: this library only implements LST_DOMode = 1, TR 18-05's confirmed
// setting for Table 14/15 (p.A-13's parameter block: "...RNDUPD=T LST_DOMode=1
// PVSATerr=F..."). Modes 0 and 2 are not implemented. Under mode 1, TR 10-09 p.5:
// "In last decompression stop time calculations, commands allowance for
// instantaneous ascent to surface from the last allowed decompression stop
// depth" — i.e. when the trial target is at or shallower than
// last_stop_depth_fsw, gas-exchange credit is given only for the portion of the
// ascent down to last_stop_depth_fsw itself; the final last_stop_depth_fsw ->
// target leg gets no additional credit.
//
// One flowchart box (the "target_depth_fsw > last_stop_depth_fsw" branch)
// literally reads "DC = DEPTH - CDEPTH" in the source, using an unexplained
// "DEPTH" variable that is never introduced elsewhere in Figure 18 or its
// supporting prose (distinct from "TDEPTH", the routine's own argument).
// Implemented here as target_depth_fsw - current_depth_fsw, matching (a) every
// other branch's TDEPTH-CDEPTH pattern in the same flowchart, and (b) FRSP7's own
// prose description of what calls into this branch for ("the IAD after ascent
// from CDepth to the new MIND is computed") — treated as the intended behavior
// per this project's standing rule (flag textual ambiguity, implement the
// well-evidenced intent), not a literal transcription of that one box's exact
// label. See DEVLOG.md for the full citation.
//
// Two mechanisms visible in the source flowchart are deliberately NOT ported:
// the "UBA Gas Transition Handler (Dive Planner only)" step (gas switching is
// out of scope for this single-inert-gas library — see generate_schedule()'s
// header comment) and the "DDAscent"/"Recycle" retry loop (only relevant to
// profiles with a deeper excursion mid-ascent, which this library's
// descend/hold/ascend walker never produces — DDAscent is always false for
// every profile this library can generate).
struct AscendrResult {
    std::vector<CompartmentState> tensions;
    double iad_fsw = 0.0;
};

inline AscendrResult ascendr(const ParameterSet& params,
                              const std::vector<CompartmentState>& entering_tensions,
                              double current_depth_fsw, double target_depth_fsw,
                              double ascent_rate_fsw_per_min) {
    const double lastop = params.last_stop_depth_fsw;

    double dc;  // signed depth change for this trial; negative = ascending.
    if (current_depth_fsw <= target_depth_fsw) {
        dc = 0.0;
    } else if (target_depth_fsw > lastop) {
        dc = target_depth_fsw - current_depth_fsw;
    } else if (current_depth_fsw > lastop) {
        dc = lastop - current_depth_fsw;  // LST_DOMode = 1
    } else {
        dc = 0.0;
    }

    std::vector<CompartmentState> trial = entering_tensions;
    if (dc != 0.0) {
        const double duration = std::fabs(dc) / ascent_rate_fsw_per_min;
        AmbientState start_amb =
            resolve_ambient_state(params, current_depth_fsw, /*dc_fsw=*/dc);
        RampRates rates = resolve_ramp_rates(params, dc / duration);  // signed rate
        for (std::size_t i = 0; i < trial.size(); ++i) {
            advance_compartment_ramp(trial[i], start_amb, rates.rinrt_fsw_per_min,
                                      rates.ro2_fsw_per_min, params.compartments[i], duration);
        }
    }

    std::vector<double> tensions_fsw(trial.size());
    for (std::size_t i = 0; i < trial.size(); ++i) tensions_fsw[i] = trial[i].tension_fsw;

    // find_ceiling_fsw()'s documented contract only ever returns a depth
    // "at or above last_stop_depth_fsw" -- called with target_depth_fsw
    // <= lastop (the LST_DOMode=1 surfacing case above), its loop guard
    // (`current > lastop`) never executes, so it silently returns
    // target_depth_fsw unchecked. That is NOT "surfacing is safe" -- it's
    // "this function doesn't evaluate that question." The actual
    // surfacing check is mptt_value()'s own SetM special case (current
    // depth == last_stop_depth_fsw redirects to the M0 row) evaluated
    // directly here: if every compartment clears that M0 threshold after
    // the credited ascent above, surfacing to target_depth_fsw is safe;
    // otherwise the ceiling is lastop (a required stop there), not 0.
    // Found via a real bug report: a shallow, long-bottom-time dive
    // (80 fsw / 100 min, XVal-He-9_023) whose entering_iad from
    // find_ceiling_fsw's frozen chain-check already equalled lastop
    // while current_depth_fsw was still well above it -- exactly the
    // condition that reaches this branch on find_first_stop_depth_fsw's
    // very first inner iteration, before any real check had ever run.
    // See DEVLOG.md, "SetM surfacing-check bug in ascendr()".
    double iad;
    if (target_depth_fsw <= lastop) {
        bool safe_to_surface = true;
        for (std::size_t i = 0; i < tensions_fsw.size(); ++i) {
            if (tensions_fsw[i] > mptt_value(params, i, lastop)) {
                safe_to_surface = false;
                break;
            }
        }
        iad = safe_to_surface ? target_depth_fsw : lastop;
    } else {
        iad = find_ceiling_fsw(params, tensions_fsw, target_depth_fsw);
    }

    return AscendrResult{std::move(trial), iad};
}

// TR 10-09 Figure 11 (FRSP7 routine) — also a real port (same page render batch
// as Figure 18, fully legible, cross-checked box by box against two independent
// 300 DPI crops). Refines a starting ceiling estimate (entering_iad_fsw — this
// library uses find_ceiling_fsw()'s result, which plays the same role TR 10-09's
// "Update IAD" / Figure 37 plays as FRSP7's input in the source) by iteratively
// testing shallower trial depths via ascendr(), which gives real gas-exchange
// credit during each hypothetical ascent. find_ceiling_fsw() alone checks frozen
// (pre-ascent) tensions against progressively shallower MPTT rows with NO credit
// at all — this is the mechanism that was missing from every round of this
// project's Table 14 investigation through round 5 (see DEVLOG.md).
//
// TR 10-09 p.17: "the first stop depth returned by FRSP7 is always an integer
// multiple of DINC," except when "after an ascent without required decompression
// stops, the first stop depth found in this routine, the IAD, may be deeper than
// the entering CDepth. In such cases the first stop depth is arbitrarily set
// equal to the entering CDepth" — implemented via the final
// std::min(mind, current_depth_fsw).
//
// Bounded outer/inner iteration guards are a defensive addition, this project's
// standard pattern for every ported flowchart loop (e.g. compartment.h's
// advance_compartment_ramp), not evidence the source's own LASTIT-latch
// convergence argument is in doubt.
inline double find_first_stop_depth_fsw(const ParameterSet& params,
                                         const std::vector<CompartmentState>& entering_tensions,
                                         double current_depth_fsw, double entering_iad_fsw,
                                         double ascent_rate_fsw_per_min) {
    if (current_depth_fsw <= entering_iad_fsw) return current_depth_fsw;

    const double dinc = params.stop_depth_increment_fsw;
    const double lastop = params.last_stop_depth_fsw;

    double iad = entering_iad_fsw;
    double mind = iad;
    bool lastit = false;

    for (int outer = 0; outer < 100; ++outer) {
        mind = iad;
        double tdinc = dinc;

        for (int inner = 0; inner < 100; ++inner) {
            const double tadepth = (iad <= lastop) ? 0.0 : (mind + tdinc);
            AscendrResult res = ascendr(params, entering_tensions, current_depth_fsw,
                                         tadepth, ascent_rate_fsw_per_min);
            iad = res.iad_fsw;

            if (iad <= mind && tdinc > 0.0 && mind > lastop) {
                tdinc = 0.0;
                continue;
            }
            break;
        }

        if (iad == mind) break;
        if (iad < mind) lastit = true;
        if (iad < mind || !lastit) {
            continue;  // restart outer loop with mind = iad (set at the top)
        }
        break;
    }

    return std::min(mind, current_depth_fsw);
}

// Multi-compartment STIM7 aggregation (TR 10-09 Figure 28's DO loop,
// pixel-confirmed): the required hold time at the given ambient state
// before ascent to a stop whose per-compartment ceilings are
// targets_fsw is safe for every compartment. TSat (saturating
// compartments) accumulates via MIN — the most restrictive upper
// bound on how long you may safely remain; TDsat (desaturating
// compartments) accumulates via MAX — the most restrictive lower
// bound on how long you must remain; the result is MIN(TSat, TDsat).
// tensions_fsw/targets_fsw are indexed the same as params.compartments.
inline double required_stop_time_all(const ParameterSet& params,
                                      const std::vector<double>& tensions_fsw,
                                      const AmbientState& amb,
                                      const std::vector<double>& targets_fsw) {
    double t_sat = std::numeric_limits<double>::infinity();
    double t_dsat = 0.0;
    for (std::size_t i = 0; i < params.compartments.size(); ++i) {
        const double t = required_stop_time(tensions_fsw[i], amb, params.compartments[i],
                                             targets_fsw[i]);
        if (tensions_fsw[i] <= amb.pan2_fsw) {
            t_sat = std::min(t_sat, t);
        } else {
            t_dsat = std::max(t_dsat, t);
        }
    }
    return std::min(t_sat, t_dsat);
}

// Refines a candidate stop hold time downward by accounting for gas
// washout during the REAL (ramping) ascent from current_depth_fsw to
// next_depth_fsw, rather than assuming next_depth_fsw is reached
// instantaneously right after the hold — TR 10-09 Figure 29 ("Stop
// Time Optimization"): "the initially computed stop time is
// shortened... to take advantage of the compartmental gas washout
// that occurs during ascent". naive_targets_fsw is the same target
// vector required_stop_time_all() was called with (the constraint
// governing current_depth_fsw -> next_depth_fsw); naive_hold_time_min
// is its result, assumed already safe.
//
// NOT a transliteration of Figure 29's control flow. Its FIRSTM/IDFS
// state machine has a branch (FIRSTM==true, DFS>DEPTH) that, as far as
// could be determined from a careful 500 DPI read, loops back with an
// unchanged trial TIME and no visible state change breaking the cycle
// — apparently designed to be unreachable in valid use (the routine's
// own prose says it is only invoked when the un-optimized time is
// already known sufficient), but not something to risk transliterating
// verbatim into safety-critical code without being able to verify
// termination. Implements the same documented goal — shorten a hold
// using real ascent-time washout — via bisection instead, which
// terminates unconditionally in a bounded number of steps. See
// DEVLOG.md, "Stop Time Optimization" entry.
inline double optimize_stop_time(const ParameterSet& params,
                                  const std::vector<CompartmentState>& state_at_current_depth,
                                  double current_depth_fsw, double next_depth_fsw,
                                  const std::vector<double>& naive_targets_fsw,
                                  double naive_hold_time_min, double ascent_rate_fsw_per_min) {
    const std::size_t n = params.compartments.size();
    if (naive_hold_time_min <= 0.0) return naive_hold_time_min;

    auto safe_after = [&](double hold_time) {
        std::vector<CompartmentState> trial = state_at_current_depth;
        AmbientState hold_amb = resolve_ambient_state(params, current_depth_fsw);
        for (std::size_t i = 0; i < n; ++i) {
            advance_compartment(trial[i], hold_amb, params.compartments[i], hold_time);
        }
        AmbientState ascent_start = resolve_ambient_state(
            params, current_depth_fsw, /*dc_fsw=*/next_depth_fsw - current_depth_fsw);
        RampRates rates = resolve_ramp_rates(params, -ascent_rate_fsw_per_min);
        const double duration =
            (current_depth_fsw - next_depth_fsw) / ascent_rate_fsw_per_min;
        for (std::size_t i = 0; i < n; ++i) {
            advance_compartment_ramp(trial[i], ascent_start, rates.rinrt_fsw_per_min,
                                      rates.ro2_fsw_per_min, params.compartments[i], duration);
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (trial[i].tension_fsw > naive_targets_fsw[i]) return false;
        }
        return true;
    };

    // The naive hold time was already proven safe under the
    // instantaneous-ascent assumption; real ascent-time washout can
    // only help, but verify rather than assume, and fall back to the
    // naive value if something unexpected breaks that monotonicity.
    if (!safe_after(naive_hold_time_min)) return naive_hold_time_min;

    double lo = 0.0;
    double hi = naive_hold_time_min;
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) / 2.0;
        if (safe_after(mid)) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return hi;
}

// A single decompression stop: depth and hold time, NOT including
// travel/ascent time to reach it — matching TR 18-05's own convention
// ("Stop times do not include travel time", e.g. Table 14).
struct ScheduleStop {
    double depth_fsw = 0.0;
    double hold_time_min = 0.0;
};

struct Schedule {
    std::vector<ScheduleStop> stops;
    double total_stop_time_min = 0.0;  // sum of hold_time_min (TR 18-05's TST)
};

// Applies one gas leg's mode (DiveSegment::gas / po2_setpoint) to a base
// ParameterSet: overrides constant_po2/po2_setpoint_atm/fn2, then
// re-resolves compartments through resolve_compartment_spec() for THAT
// leg's fn2 — CNDSDR_FO2 gating is a per-leg effect, not a single
// dive-wide one (TR 10-09 Figure 32). E.g. VVal-79's SDR=0.70 is gated
// OFF while breathing air (fn2=0.79 > 1-cndsdr_fo2=0.20, TR 12-01
// Table B.2) but gated ON once switched to O2 (fn2=0.20 <= 0.20) — see
// generate_schedule()'s gas-switching header comment below.
// cndsdr_fo2 itself is a fixed regime characteristic (TR 12-01 Table
// B.2 lists it as an algorithm parameter, not a per-gas one) and is
// carried through from `base` unchanged.
//
// `original` MUST be the regime's pristine, never-gated ParameterSet
// (i.e. generate_schedule()'s own `params_in`, untouched by any prior
// resolve_compartment_spec() call) — NOT `base`. resolve_compartment_spec()
// can only ever FORCE sdr to 1.0, never restore a real table value once
// it's been forced (compartment.h) — so resolving from `base.compartments`
// (which may already carry a PREVIOUS leg's gated sdr, e.g. forced to 1.0
// while breathing air) would permanently lose the ability to re-enable
// sdr on a later leg that should pass the gate (e.g. switching to O2).
// This was a real, confirmed bug: every leg-switching/O2-cycling schedule
// was silently stuck at sdr=1.0 throughout, regardless of the active
// gas, because the FIRST resolution (for the bottom/travel gas) baked in
// sdr=1.0 and no later leg could ever recover VVal-79's real 0.70 — see
// DEVLOG.md, "Fixed a destructive SDR-gating bug in leg switching."
inline ParameterSet apply_gas_leg(const ParameterSet& base, const DiveSegment& leg,
                                   const ParameterSet& original) {
    ParameterSet resolved = base;
    if (leg.po2_setpoint.has_value()) {
        resolved.constant_po2 = true;
        resolved.po2_setpoint_atm = *leg.po2_setpoint;
    } else {
        resolved.constant_po2 = false;
        resolved.fn2 = leg.gas.fn2();
    }
    for (std::size_t i = 0; i < resolved.compartments.size(); ++i) {
        resolved.compartments[i] = resolve_compartment_spec(
            original.compartments[i], resolved.constant_po2, resolved.fn2, base.cndsdr_fo2);
    }
    return resolved;
}

// TR 10-09 Figure 13 ("O2-Air Cycling") state — persists across the
// whole decompression phase of one schedule, unlike ascendr()/
// find_first_stop_depth_fsw() (called fresh each time). Real port —
// fully legible at 300 DPI (PDF page 25, sources/ADA549883.pdf), cross-
// checked against two independent crops, same standard as FRSP7/
// Ascendr. Flag names match the source exactly (O2STRTD, STRTO2, ONO2,
// RTIME, DEAD_TIM, PSWO2) for direct comparison against the flowchart.
struct O2AirCyclingState {
    bool o2strtd = false;  // has the O2-decompression procedure already been triggered
    bool strto2 = true;    // O2 decompression is armed for this profile (this library's
                            // stand-in for the OD/IW PCF option, which isn't modeled)
    bool ono2 = false;     // currently breathing O2 (vs. air)
    double rtime = 0.0;    // remaining time in the current gas period
    bool dead_tim = false; // current/most-recent period is decompression dead time —
                            // tracked for fidelity; TR 10-09's own flowchart sets but
                            // never reads it within Figure 13 itself (used elsewhere in
                            // TBLP7R for separate reporting this library doesn't need).
};

// Computes the total hold time at current_depth_fsw under O2-Air
// Cycling, mutating both `state` (tissue tensions, advanced through
// every gas sub-period) and `cyc` (persists to the next call). This
// owns the full Figure 13 loop internally, including the "Compute Stop
// TIME" sub-box (this library's required_stop_time_all()) and, for the
// final sub-period only, TR 10-09 Figure 29's ascent-washout
// optimization and Figure 16's RNDUPD rounding — matching exactly what
// the non-cycling path in generate_schedule() already does for a
// single-gas stop.
//
// Caller contract: only call this when current_depth_fsw <=
// params.o2ceil_fsw (Figure 13's own "CDEPTH <= O2CEIL" guard) — this
// function assumes O2 decompression is at least eligible to start or
// continue here.
//
// Not ported: the "GSWLat Accommodated or GSWLAT=0" latency-
// accommodation branch is implemented (PSWO2/gs_dead bookkeeping), but
// VVal-79 (gswlat_min=0, TR 12-01 Table B.2) never exercises it — the
// OR's second operand is always true for this regime, same character
// as Ascendr's DDAscent/Recycle branch being unreachable for this
// library's walker. Kept faithful rather than deleted in case a future
// regime has nonzero GSWLAT.
// `original_params` is generate_schedule()'s pristine `params_in`, passed
// through unchanged so every apply_gas_leg() call below resolves SDR from
// the regime's real table values rather than an already-gated copy — see
// apply_gas_leg()'s header comment above for why this matters.
inline double run_o2_air_cycling_hold(const ParameterSet& base_params,
                                       std::vector<CompartmentState>& state,
                                       O2AirCyclingState& cyc, double current_depth_fsw,
                                       double next_depth_fsw, double ascent_rate_fsw_per_min,
                                       const ParameterSet& original_params) {
    const std::size_t n = state.size();
    const double air_fn2 = base_params.fn2;  // AIR_FN2: the bottom/travel gas's own fn2
    // reported_hold is what this function returns (and what generate_schedule()
    // records as the stop's hold_time_min) -- TR 10-09 p.50: "the in-water
    // oxygen decompression schedule appears... with stop times that indicate
    // ONLY the oxygen breathing time..." Explicitly confirmed: "With the
    // AB_DEAD parameter set TRUE, in-water oxygen breathing times... are the
    // same as..." a run with air breaks disabled entirely -- i.e. the
    // TABULATED stop time excludes any period whose dead_tim flag is set
    // (air breaks under AB_DEAD, gas-switch latency under GS_DEAD). Dead
    // periods are dead for GAS EXCHANGE too, not just reporting: TR 10-09
    // Table 1 defines AB_DEAD/GS_DEAD as "gas exchange dead time", and
    // Figure 17 skips the UPDT7 tissue update entirely when DEAD_TIM is
    // set -- tensions are frozen through every dead period, which is
    // precisely what makes the p.50 with-breaks == without-breaks
    // guarantee exact. (An earlier version of this function advanced
    // tissue through dead periods on the break gas -- physically
    // intuitive, but contradicted by the source, and the confirmed root
    // cause of a 2-9 min systematic undershoot vs. TR 12-01 Appendix D
    // that grew with the number of air breaks -- see DEVLOG.md, "O2-Air
    // Cycling residual root-caused".)
    double reported_hold = 0.0;

    for (int guard = 0; guard < 1000; ++guard) {
        if (!cyc.o2strtd) {
            if (!(cyc.strto2 && current_depth_fsw <= base_params.o2ceil_fsw)) {
                // Figure 13's "STRTO2 & TIME>0 & CDEPTH<=O2CEIL: NO" —
                // TIME>0 is the caller's contract (only called when
                // there's a real hold to compute) so it's folded out
                // here rather than threaded through as a parameter.
                break;
            }
            const bool latency_accommodated = (base_params.gswlat_min <= 0.0);
            if (!latency_accommodated) {
                // "Accommodate latency": hold on the ENTERING (bottom/
                // travel) gas for gswlat_min, then loop back around —
                // Figure 13 sets O2STRTD nowhere on this path, so the
                // top check runs again next iteration and (with the
                // latency now behind us) proceeds to the GAS=O2 box.
                // Unreachable for VVal-79 (gswlat_min=0) — see header
                // comment.
                DiveSegment air_leg;
                air_leg.depth_fsw = current_depth_fsw;
                air_leg.gas = GasMix{1.0 - air_fn2, 0.0};
                ParameterSet air_params = apply_gas_leg(base_params, air_leg, original_params);
                AmbientState amb = resolve_ambient_state(air_params, current_depth_fsw);
                const double lat = base_params.gswlat_min;
                // Under GS_DEAD, the latency is "gas exchange dead time"
                // (TR 10-09 Table 1) — tissue tensions are frozen for the
                // whole period, exactly like an AB_DEAD air break (Figure
                // 17 skips the UPDT7 tissue update when DEAD_TIM is set).
                if (!base_params.gs_dead) {
                    for (std::size_t i = 0; i < n; ++i) {
                        advance_compartment(state[i], amb, air_params.compartments[i], lat);
                    }
                    reported_hold += lat;
                }
                cyc.dead_tim = base_params.gs_dead;
                continue;
            }
            cyc.ono2 = true;
            cyc.rtime = base_params.o2time_min - base_params.gswlat_min;
            cyc.strto2 = false;
            cyc.o2strtd = true;
            // Falls through to "Compute Stop TIME" below, same as the
            // O2STRTD==true entry path.
        }

        if (cyc.dead_tim && cyc.rtime > 0.0) {
            // Dead period (an AB_DEAD air break; a GS_DEAD latency is
            // handled on its own path above): TR 10-09 Table 1 defines
            // AB_DEAD as "Consider air breaks as GAS EXCHANGE dead time",
            // and Figure 17 ("Update/Record model state", p.23/PDF p.28)
            // routes DEAD_TIM=TRUE around the UPDT7 tissue-update call
            // entirely — compartmental tensions are FROZEN for the whole
            // period, not advanced on the break gas. A frozen period can
            // never satisfy any part of the stop requirement, so it is
            // consumed wholesale (no stop-time computation on the break
            // gas) and the requirement resumes on the next live O2
            // period. This is what makes TR 10-09 p.50's guarantee exact:
            // with AB_DEAD=TRUE, reported O2-breathing time WITH air
            // breaks equals the same computation WITHOUT them. See
            // DEVLOG.md, "O2-Air Cycling residual root-caused: dead
            // periods must freeze gas exchange."
            cyc.rtime = 0.0;
            continue;  // rtime<=0 logic flips back to O2 next iteration
        }

        const double active_fn2 = cyc.ono2 ? (1.0 - base_params.o2time_fo2) : air_fn2;
        DiveSegment leg;
        leg.depth_fsw = current_depth_fsw;
        leg.gas = GasMix{1.0 - active_fn2, 0.0};
        ParameterSet gas_params = apply_gas_leg(base_params, leg, original_params);

        AmbientState amb = resolve_ambient_state(gas_params, current_depth_fsw);
        std::vector<double> tensions(n), targets(n);
        for (std::size_t i = 0; i < n; ++i) {
            tensions[i] = state[i].tension_fsw;
            targets[i] = mptt_value(gas_params, i, current_depth_fsw);
        }
        double time = required_stop_time_all(gas_params, tensions, amb, targets);
        if (!std::isfinite(time)) break;  // no finite hold satisfies every compartment

        bool recycle = false;
        if (cyc.rtime <= 0.0) {
            if (cyc.ono2) {
                if (time <= 5.0 && current_depth_fsw == base_params.last_stop_depth_fsw) {
                    cyc.rtime = base_params.o2time_min;
                    // Falls through with the full, uncapped `time` as
                    // this pass's answer (Figure 13: this box's arrow
                    // goes straight to Round-Time-Up, bypassing the
                    // TIME>RTIME check — already guaranteed unneeded
                    // since time<=5<=o2time_min).
                } else {
                    cyc.ono2 = false;
                    cyc.rtime = base_params.airtime_min;
                    cyc.dead_tim = base_params.ab_dead;
                    continue;  // re-loop: next pass computes on AIR
                }
            } else {
                cyc.ono2 = true;
                cyc.rtime = base_params.o2time_min;
                cyc.dead_tim = false;
                continue;  // re-loop: next pass computes on O2
            }
        } else if (time > cyc.rtime) {
            time = cyc.rtime;
            recycle = true;
        }

        if (!recycle) {
            // The real requirement is satisfied within this gas
            // period — final sub-period. Apply the same washout
            // optimization and rounding the non-cycling path uses.
            time = optimize_stop_time(gas_params, state, current_depth_fsw, next_depth_fsw,
                                       targets, time, ascent_rate_fsw_per_min);
            if (base_params.round_stop_time_up && time > 0.0) {
                time = std::ceil(time - 1e-9);
            }
            if (time > 0.0) {
                for (std::size_t i = 0; i < n; ++i) {
                    advance_compartment(state[i], amb, gas_params.compartments[i], time);
                }
                if (!cyc.dead_tim) reported_hold += time;
                cyc.rtime -= time;
            }
            break;
        }

        // Capped by RTIME: consume the whole remaining period on the
        // active gas, then loop to switch gas and continue.
        if (time > 0.0) {
            for (std::size_t i = 0; i < n; ++i) {
                advance_compartment(state[i], amb, gas_params.compartments[i], time);
            }
            if (!cyc.dead_tim) reported_hold += time;
        }
        cyc.rtime = 0.0;
    }

    // RNDUPD (TR 10-09 Figure 16) on the stop's reported O2-breathing
    // total. The single-gas path rounds its one hold up to a whole
    // minute (see generate_schedule()); a cycling stop's total is the
    // sum of one or more O2 sub-periods, and its capped (non-final)
    // sub-periods end at fractional RTIME boundaries once the O2 clock
    // has been decremented by inter-stop travel (Figure 9's "RTIME =
    // RTIME - TC"), so the raw sum is generally fractional. Round it up
    // here so every reported stop time is a whole minute, matching the
    // AIR path and this library's documented TTIS=F convention (see
    // ParameterSet::round_stop_time_up). Tissue state was already
    // advanced through the real (fractional) sub-period lengths above —
    // this rounds only the reported number, not the model timeline.
    if (base_params.round_stop_time_up && reported_hold > 0.0) {
        reported_hold = std::ceil(reported_hold - 1e-9);
    }
    return reported_hold;
}

// Generates a greedy decompression schedule: descend to max_depth_fsw
// (ramping at descent_rate_fsw_per_min), hold at max_depth_fsw for the
// remainder of bottom_time_min (see the bottom-hold note below), then
// repeatedly ascend to the current ceiling and hold at each stop for
// the time required before the next shallower stop is safe, until
// surfaced. This is the minimal greedy walker the implementation
// spec's §4.1 describes: "ascend to the current ceiling..., hold at
// fixed stop depths... until the ceiling clears the next-shallower
// stop, repeat to surface" — built from this file's/compartment.h's
// primitives (resolve_ambient_state, advance_compartment[_ramp],
// find_ceiling_fsw, required_stop_time_all), not a transliteration of
// TBLP7R's control flow.
//
// GAS SWITCHING (deco_legs): TR 10-09's PCF format (Appendix B) natively
// models a dive as a sequence of records, each on its own gas — that's
// the source's own structure for what this parameter exposes. Each
// DiveSegment in deco_legs is a SWITCH TRIGGER during ascent, not a
// held stage: depth_fsw is the depth at which this leg's gas becomes
// active (once current_depth drops to or below it), and gas/po2_setpoint
// describe the new mode; time_min is unused here (DiveSegment is reused
// from profile.h rather than introducing a parallel type — see that
// file's header comment). Legs are sorted by depth_fsw descending
// internally, so callers don't need to pre-sort. Tissue state carries
// through a switch unmodified — only the AmbientState/RampRates formulas
// used going forward change (TR 10-09's own single continuous
// tissue-timeline model); see apply_gas_leg() above for the CNDSDR_FO2
// re-resolution this requires. Legs only ever apply during ascent
// (matching TR 10-09's OD/IW option prose: "the gas setting specified by
// the leading P* or F* option... is used until O2CEIL is reached during
// the ascent") — never during descent or the bottom hold. An empty
// deco_legs reproduces the original single-gas behavior exactly.
//
// deco_legs is a MANUAL/explicit gas-switching mechanism — the caller
// decides switch depths and gas modes. TR 10-09 Figure 13's AUTOMATIC
// O2-Air Cycling decompression procedure (O2CEIL-triggered, GSWLAT
// latency, O2TIME/AIRTIME cycling, GS_DEAD/AB_DEAD dead-time accounting)
// is a separate mechanism, gated by ParameterSet::o2_cycling_enabled —
// see run_o2_air_cycling_hold() above, which this function calls
// automatically once current_depth_fsw <= o2ceil_fsw when enabled
// (independent of deco_legs; the two mechanisms are not combined in one
// call). Matches TR 12-01 Appendix D's AIR/O2 tables to within ±1 min
// per stop / ±2 min per row across the full 100 and 110 fsw depth
// blocks (31 rows), in the table's own printed convention, after fixing
// two real bugs: a safety-critical SDR-gating bug (DEVLOG.md, "Fixed a
// destructive SDR-gating bug in leg switching") and a dead-time bug
// where air breaks advanced tissue state instead of freezing it
// (DEVLOG.md, "O2-Air Cycling residual root-caused"; TR 10-09 Table 1 +
// Figure 17). See the golden test in tests/test_schedule.cpp.
//
// NOT modeled (spec §10 pitfall 4, out of scope): bell lock-in holds,
// air breaks, travel-time-in-stops bookkeeping, repetitive-group logic.
// Single-inert-gas PER LEG (spec pitfall 1/3 no longer applies dive-wide
// now that deco_legs exists, but each leg is still single-inert-gas):
// there's no modeled gas switch during descent even where real
// operational profiles use one (e.g. TR 18-05's 0.7->1.3 atm PO2 switch
// at 32 fsw, confirmed also in the US Navy Diving Manual Rev 7 Chapter
// 15 with slightly different numbers — 0.75 atm/33 fsw — see
// TABLE14_COMPARISON.md §6.5) — descent legs are not modeled by
// deco_legs. descent_rate_fsw_per_min/ascent_rate_fsw_per_min are
// magnitudes (positive); sign is applied internally.
inline Schedule generate_schedule(const ParameterSet& params_in, double max_depth_fsw,
                                   double bottom_time_min, double descent_rate_fsw_per_min,
                                   double ascent_rate_fsw_per_min, const Profile& deco_legs) {
    // Resolve the CNDSDR_FO2 gate once up front for the bottom/descent
    // gas — see resolve_open_circuit_compartments()'s header comment.
    // `params` is mutable: apply_gas_leg() reassigns it wholesale when a
    // deco_legs trigger is reached during ascent, and every use below
    // (including inside ascendr()/required_stop_time_all()/
    // optimize_stop_time(), which all read params.compartments directly,
    // and the ramp_ascend lambda, which captures params by reference)
    // sees the currently-active leg's resolved compartments as a result.
    ParameterSet params = resolve_open_circuit_compartments(params_in);
    const std::size_t n = params.compartments.size();
    std::vector<CompartmentState> state(n);

    // Deco legs sorted deepest-trigger-first, so a simple advancing
    // index correctly applies them in the order an ascent reaches them
    // regardless of the order the caller passed them in.
    std::vector<DiveSegment> legs(deco_legs.begin(), deco_legs.end());
    std::sort(legs.begin(), legs.end(),
              [](const DiveSegment& a, const DiveSegment& b) { return a.depth_fsw > b.depth_fsw; });
    std::size_t next_leg = 0;
    auto apply_due_legs = [&](double depth_fsw) {
        while (next_leg < legs.size() && depth_fsw <= legs[next_leg].depth_fsw + 1e-9) {
            params = apply_gas_leg(params, legs[next_leg], params_in);
            ++next_leg;
        }
    };

    // Initial tissue loading: saturated on AIR at the surface, computed
    // through the model's own arterial-tension formula — see
    // air_saturation_tension_fsw() and TR 10-09 Appendix B p.B-1. This
    // is ~PACO2 below the earlier round-5 approximation
    // (0.79*surface_pressure_fsw), the difference being the arterial
    // CO2/H2O/AMBAO2 offset the tissue actually equilibrates against;
    // for XVal-He-8_040 that is 24.57 vs 26.07 fsw. The excess decays
    // with each compartment's own half-time and is what previously left
    // the slow (200-min) compartment ~1.5 fsw over-loaded at the stop
    // where it becomes binding — see DEVLOG.md's Table 14 round 7 entry.
    {
        const double initial_tension_fsw = air_saturation_tension_fsw(params);
        for (auto& s : state) s.tension_fsw = initial_tension_fsw;
    }

    // Descent: ramp from the surface to max_depth_fsw.
    const double descent_duration_min = max_depth_fsw / descent_rate_fsw_per_min;
    {
        AmbientState start_amb = resolve_ambient_state(params, 0.0, /*dc_fsw=*/max_depth_fsw);
        RampRates rates = resolve_ramp_rates(params, descent_rate_fsw_per_min);
        for (std::size_t i = 0; i < n; ++i) {
            advance_compartment_ramp(state[i], start_amb, rates.rinrt_fsw_per_min,
                                      rates.ro2_fsw_per_min, params.compartments[i],
                                      descent_duration_min);
        }
    }

    // Bottom hold. bottom_time_min is the diver-facing "Bottom Time" —
    // total elapsed time from leaving the surface to leaving the bottom
    // (US Navy Diving Manual Rev 7 §9-3.2, and Chapter 15's restatement
    // for 1.3 ata ppO2 He-O2 EC-UBA diving specifically: "the bottom
    // time (time from leaving the surface to leaving the bottom)") —
    // not a pure isobaric-hold duration on top of descent. The actual
    // hold at max_depth_fsw is therefore bottom_time_min minus the
    // descent time already spent getting there, floored at 0 (a
    // bottom_time_min shorter than the descent itself means the diver
    // never reaches a steady hold before beginning ascent).
    const double hold_duration_min = std::max(bottom_time_min - descent_duration_min, 0.0);
    {
        AmbientState amb = resolve_ambient_state(params, max_depth_fsw);
        for (std::size_t i = 0; i < n; ++i) {
            advance_compartment(state[i], amb, params.compartments[i], hold_duration_min);
        }
    }

    auto ramp_ascend = [&](double from_depth, double to_depth, const ParameterSet& gas_params) {
        AmbientState start_amb =
            resolve_ambient_state(gas_params, from_depth, /*dc_fsw=*/to_depth - from_depth);
        RampRates rates = resolve_ramp_rates(gas_params, -ascent_rate_fsw_per_min);
        const double duration = (from_depth - to_depth) / ascent_rate_fsw_per_min;
        for (std::size_t i = 0; i < n; ++i) {
            advance_compartment_ramp(state[i], start_amb, rates.rinrt_fsw_per_min,
                                      rates.ro2_fsw_per_min, gas_params.compartments[i], duration);
        }
    };

    Schedule schedule;
    double current_depth = max_depth_fsw;
    const double dinc = params.stop_depth_increment_fsw;

    // TR 10-09 Figure 13 ("O2-Air Cycling") state, persisting across
    // every stop of the ascent — see run_o2_air_cycling_hold(). strto2
    // (O2 decompression "armed") mirrors params.o2_cycling_enabled,
    // this library's stand-in for the OD/IW PCF option; the rest of
    // O2AirCyclingState's defaults are already the correct starting
    // state (not yet triggered, not yet on O2).
    O2AirCyclingState cyc;
    cyc.strto2 = params.o2_cycling_enabled;

    // Bounded defensive guard, not an expected iteration count for any
    // realistic profile — see compartment.h's Regime loop for the same
    // pattern/rationale.
    for (int guard = 0; guard < 500 && current_depth > 0.0; ++guard) {
        // Apply any deco-gas-leg switch whose trigger depth has been
        // reached — covers both the state at loop entry (including the
        // first iteration, if a leg triggers at or above max_depth_fsw)
        // and after the previous iteration's ascent to next_depth.
        apply_due_legs(current_depth);

        std::vector<double> tensions(n);
        for (std::size_t i = 0; i < n; ++i) tensions[i] = state[i].tension_fsw;

        // TR 10-09 Figure 37 ("Update IAD") gives the entering estimate;
        // Figure 11 (FRSP7) refines it with real gas-exchange credit
        // during the candidate ascent itself — see find_first_stop_depth_fsw()'s
        // header comment. Applied at every iteration, not just the first
        // ascent from bottom: FRSP7's role in TBLP7R's overall loop is to
        // find the next required stop depth generally, not a one-time
        // start-of-dive computation.
        const double entering_iad = find_ceiling_fsw(params, tensions, current_depth);
        const double ceiling = find_first_stop_depth_fsw(
            params, state, current_depth, entering_iad, ascent_rate_fsw_per_min);
        if (ceiling < current_depth) {
            ramp_ascend(current_depth, ceiling, params);
            current_depth = ceiling;
            apply_due_legs(current_depth);
            for (std::size_t i = 0; i < n; ++i) tensions[i] = state[i].tension_fsw;
        }
        if (current_depth <= 0.0) break;

        AmbientState amb = resolve_ambient_state(params, current_depth);
        std::vector<double> targets(n);
        for (std::size_t i = 0; i < n; ++i) targets[i] = mptt_value(params, i, current_depth);

        // Per the SetM convention (see mptt_value()'s header comment),
        // ascending from last_stop_depth_fsw always targets the
        // surface directly, not last_stop-DINC.
        const double next_depth = (current_depth <= params.last_stop_depth_fsw + 1e-9)
                                       ? 0.0
                                       : std::max(current_depth - dinc, 0.0);

        double hold_time;
        if (params.o2_cycling_enabled && current_depth <= params.o2ceil_fsw + 1e-9) {
            // TR 10-09 Figure 13: run_o2_air_cycling_hold() owns the
            // full sub-loop (gas selection, STIM7-equivalent
            // computation, Figure 29 washout optimization, and RNDUPD
            // rounding for the final sub-period) and advances `state`
            // itself — unlike the single-gas path below, there is no
            // separate advance_compartment() call afterward.
            hold_time = run_o2_air_cycling_hold(params, state, cyc, current_depth, next_depth,
                                                 ascent_rate_fsw_per_min, params_in);
        } else {
            hold_time = required_stop_time_all(params, tensions, amb, targets);
            if (!std::isfinite(hold_time)) {
                // No finite hold satisfies every compartment's constraint
                // (shouldn't happen for a well-formed table/profile) —
                // stop rather than loop or fabricate a schedule.
                break;
            }
            // TR 10-09 Figure 29: shorten the hold using the washout that
            // happens during the real (ramping) ascent to next_depth,
            // rather than assuming next_depth is reached instantaneously.
            hold_time = optimize_stop_time(params, state, current_depth, next_depth, targets,
                                            hold_time, ascent_rate_fsw_per_min);
            // TR 10-09 Figure 16 ("Round Time Up"), gated on RNDUPD: round
            // the hold up to the next whole minute and advance the tissues
            // with that rounded value (the -1e-9 keeps an already-integer
            // time from stepping to the next minute on floating-point
            // fuzz). See ParameterSet::round_stop_time_up.
            if (params.round_stop_time_up && hold_time > 0.0) {
                hold_time = std::ceil(hold_time - 1e-9);
            }
            if (hold_time > 0.0) {
                for (std::size_t i = 0; i < n; ++i) {
                    advance_compartment(state[i], amb, params.compartments[i], hold_time);
                }
            }
        }
        if (hold_time > 0.0) {
            schedule.stops.push_back({current_depth, hold_time});
            schedule.total_stop_time_min += hold_time;
        }

        if (params.o2_cycling_enabled && cyc.o2strtd) {
            // TR 10-09 Figure 9: once O2-Air Cycling has started, travel
            // between stops is breathed on the prevailing cycle gas (O2
            // mid-period, air mid-break), not the bottom/travel gas, and
            // RTIME is decremented by the travel time ("Update RTIME:
            // RTIME = RTIME - TC" on the travel path). This also keeps the
            // real ascent consistent with the washout gas
            // optimize_stop_time() already assumed when it trimmed the
            // final sub-period inside run_o2_air_cycling_hold(). Not
            // modeled (sub-travel-time effect, ~1/3 min for a 10 fsw
            // increment): Figure 9's mid-travel period-expiry split
            // ("DEPTH = RATE*RTIME + CDEPTH" when TC > RTIME) — a period
            // expiring mid-travel switches gas at the period boundary in
            // the source; here the switch lands at the next stop instead.
            DiveSegment cycle_leg;
            cycle_leg.depth_fsw = current_depth;
            const double cycle_fn2 = cyc.ono2 ? (1.0 - params.o2time_fo2) : params.fn2;
            cycle_leg.gas = GasMix{1.0 - cycle_fn2, 0.0};
            const ParameterSet cycle_params = apply_gas_leg(params, cycle_leg, params_in);
            ramp_ascend(current_depth, next_depth, cycle_params);
            cyc.rtime -= (current_depth - next_depth) / ascent_rate_fsw_per_min;
        } else {
            ramp_ascend(current_depth, next_depth, params);
        }
        current_depth = next_depth;
    }

    return schedule;
}

// Convenience overload: single fixed gas mode for the entire dive, no
// gas switching — equivalent to calling the above with an empty
// deco_legs list. Preserved with its original signature so every
// existing single-gas call site is unaffected by deco_legs' addition.
inline Schedule generate_schedule(const ParameterSet& params, double max_depth_fsw,
                                   double bottom_time_min, double descent_rate_fsw_per_min,
                                   double ascent_rate_fsw_per_min) {
    return generate_schedule(params, max_depth_fsw, bottom_time_min, descent_rate_fsw_per_min,
                              ascent_rate_fsw_per_min, Profile{});
}

// EL-DCM engine: given a ParameterSet and a profile, computes tissue
// loadings and a decompression schedule. Implemented starting Phase 1.
class EldcmEngine {
   public:
    explicit EldcmEngine(ParameterSet params) : params_(std::move(params)) {}

    const ParameterSet& params() const { return params_; }

   private:
    ParameterSet params_;
};

}  // namespace deco
