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

    // LST_DOMode (TR 10-09 Table 1 p.5). Last-stop TIME to the surface:
    //   0 — instantaneous ascent from DINC (not implemented here)
    //   1 — instantaneous ascent from last_stop_depth_fsw (no washout
    //       credit on last_stop → 0). VVal-79 Table B.2; TR 18-05 Table 14.
    //   2 — credit gas exchange on the entire last_stop → surface ascent.
    int lst_domode = 1;

    // RNDUPD (TR 10-09 Figure 16, "Round Time Up"): when true, each
    // computed stop time is rounded UP to the next whole minute before
    // it is applied. This is not merely output formatting — Figure 16
    // returns occupancy for UPDT7. With TTIS=F / FRSTOP that occupancy
    // equals the printed cell (ceil of STIM7 TIME). With TTIS=T on a
    // later stop it is ceil(TIME+TT)−TT. TR 18-05 p.A-13 lists RNDUPD=T
    // for Table 14. Set false only to inspect unrounded STIM7 TIME.
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

    // TTIS (TR 10-09 Table 1 p.6): when true, travel time to each stop
    // except the first air stop and the first O2 stop is included in the
    // printed stop time. Figure 16 adds TT, ceils, then subtracts TT so
    // UPDT7 occupancy is ceil(TIME+TT)−TT. VVal-79 Table B.2 TTIS=TRUE.
    // XVal-He Table 14/7 keep the default false (occupancy printed).
    bool ttis = false;

    // OMIT_TRVL (TR 10-09 Table 1 p.7): when true, travel is omitted
    // from elapsed ONO2/ONAIR (RTIME) during Air/O2 cycling. Tissue
    // update during travel is unchanged. VVal-79 Table B.2 TRUE.
    bool omit_trvl = false;

    // STIME (TR 10-09 Table 1; TR 12-01 Table B.2 = 0.2 min). Maximum
    // duration of one live UPDT7 call on a recorded descent/travel ramp.
    // Figure 9 is one UPDT7(CDEPTH, DC, TC=DC/RATE) per travel; ANS-file
    // generation chops segments at STIME. With stime_min > 0 each chop is
    // a closed-form ramp of duration ≤ STIME (UPDT7 with DC = rate·dt).
    // stime_min <= 0 keeps a single closed-form ramp (XVal-He default).
    double stime_min = 0.0;
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

// Live-profile ramp (descent / travel). stime_min <= 0: one closed-form
// UPDT7 call for the whole leg. stime_min > 0: the same closed-form
// update in pieces of duration ≤ STIME (Figure 9 UPDT7 with DC=rate·dt).
// Hypothetical trials (Ascendr / FRSP7 / Figure 29) stay one-shot —
// Figure 18 is a single UPDT7.
inline void advance_profile_ramp(std::vector<CompartmentState>& state,
                                  const ParameterSet& params, double from_depth_fsw,
                                  double to_depth_fsw, double rate_mag_fsw_per_min) {
    if (from_depth_fsw == to_depth_fsw || rate_mag_fsw_per_min <= 0.0)
        return;
    const std::size_t n = state.size();
    const double signed_rate =
        (to_depth_fsw > from_depth_fsw) ? rate_mag_fsw_per_min : -rate_mag_fsw_per_min;
    auto one_ramp = [&](double d0, double d1) {
        AmbientState start_amb = resolve_ambient_state(params, d0, d1 - d0);
        RampRates rates = resolve_ramp_rates(params, signed_rate);
        const double duration = std::fabs(d1 - d0) / rate_mag_fsw_per_min;
        for (std::size_t i = 0; i < n; ++i) {
            advance_compartment_ramp(state[i], start_amb, rates.rinrt_fsw_per_min,
                                      rates.ro2_fsw_per_min, params.compartments[i], duration);
        }
    };
    if (params.stime_min <= 0.0) {
        one_ramp(from_depth_fsw, to_depth_fsw);
        return;
    }
    double d = from_depth_fsw;
    for (int guard = 0; guard < 100000; ++guard) {
        const double remaining = to_depth_fsw - d;
        if (std::fabs(remaining) <= 1e-12)
            break;
        const double dt = std::min(params.stime_min, remaining / signed_rate);
        one_ramp(d, d + signed_rate * dt);
        d += signed_rate * dt;
    }
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
// LST_DOMode 1 is TR 18-05 Table 14/15 and VVal-79 Table B.2: when the
// trial target is at or shallower than last_stop_depth_fsw, gas-exchange
// credit is given only down to last_stop_depth_fsw; the final last_stop
// → target leg is instantaneous. Mode 2 (Figure 18) credits the entire
// last_stop → target ascent. Mode 0 is not implemented.
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
    } else if (params.lst_domode == 2) {
        // Figure 18: credit gas exchange on the entire last_stop → target
        // (surface) leg. VVal-79 Table B.2 is mode 1; mode 2 is the
        // Figure 29 last-stop path.
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

// TR 10-09 Figure 29 ("Stop Time Optimization"), FIRSTM/IDFS search.
// Store tensions, trial UPDT7(CDEPTH, DC=0, TC=TIME), FRSP7 → DFS, restore.
//   DFS ≤ DEPTH and (IDFS or TIME=0) → return TIME
//   DFS ≤ DEPTH and not yet IDFS      → TIME = max(TIME − 0.1, 0), FIRSTM=false
//   DFS > DEPTH and not FIRSTM        → TIME += 0.005, IDFS=true
//   DFS > DEPTH and FIRSTM            → return the entering STIM7 TIME
// The FIRSTM=true / DFS>DEPTH box in the flowchart loops with unchanged
// TIME. That branch is only reached when the un-optimized time is already
// insufficient under FRSP7 (not expected — Figure 29 is invoked on a time
// already known sufficient for a smaller jump). Returning the entering
// TIME is the terminating equivalent of "do not shorten".
//
// Figure 28 invokes this only when the next stop is more than one DINC
// shallower than CDepth, or on last-stop → surface with LST_DOMode ≠ 1.
// A one-DINC hop returns the STIM7 time unchanged; RNDUPD then ceils it.
inline double optimize_stop_time(const ParameterSet& params,
                                  const std::vector<CompartmentState>& state_at_current_depth,
                                  double current_depth_fsw, double next_depth_fsw,
                                  const std::vector<double>& naive_targets_fsw,
                                  double naive_hold_time_min, double ascent_rate_fsw_per_min) {
    (void)naive_targets_fsw;
    const std::size_t n = params.compartments.size();
    if (naive_hold_time_min <= 0.0) return naive_hold_time_min;

    const bool last_stop_to_surface =
        (next_depth_fsw <= 1e-9) ||
        (current_depth_fsw <= params.last_stop_depth_fsw + 1e-9 &&
         next_depth_fsw < params.last_stop_depth_fsw);

    // LST_DOMode=1: last-stop time assumes instantaneous ascent to the
    // surface (TR 10-09 Table 1; VVal-79 Table B.2). Figure 29 washout
    // credit is not applied on last_stop → 0. Mode 2 keeps the credit.
    if (params.lst_domode == 1 && last_stop_to_surface)
        return naive_hold_time_min;

    // Figure 28: one-DINC hops use raw STIM7 TIME. Mode-2 last stop to
    // surface still gets the credit even when the hop equals DINC.
    const double hop_fsw = current_depth_fsw - next_depth_fsw;
    if (hop_fsw <= params.stop_depth_increment_fsw + 1e-9 && !last_stop_to_surface)
        return naive_hold_time_min;

    // Figure 29: TIME −= 0.1 until DFS > DEPTH, then TIME += 0.005 (IDFS).
    constexpr double kFig29CoarseMin = 0.1;
    constexpr double kFig29FineMin = 0.005;

    auto dfs_after_hold = [&](double hold_time) {
        std::vector<CompartmentState> trial = state_at_current_depth;
        AmbientState hold_amb = resolve_ambient_state(params, current_depth_fsw);
        if (hold_time > 0.0) {
            for (std::size_t i = 0; i < n; ++i)
                advance_compartment(trial[i], hold_amb, params.compartments[i], hold_time);
        }
        // Last-stop → surface: find_ceiling never drops below last_stop, so
        // FRSP7 would return CDEPTH. Figure 29 compares DFS to DEPTH=0;
        // Ascendr's IAD after the credited last_stop → 0 ramp is that DFS.
        if (next_depth_fsw <= params.last_stop_depth_fsw + 1e-9) {
            return ascendr(params, trial, current_depth_fsw, next_depth_fsw,
                           ascent_rate_fsw_per_min)
                .iad_fsw;
        }
        std::vector<double> tensions(n);
        for (std::size_t i = 0; i < n; ++i) tensions[i] = trial[i].tension_fsw;
        const double iad = find_ceiling_fsw(params, tensions, current_depth_fsw);
        return find_first_stop_depth_fsw(params, trial, current_depth_fsw, iad,
                                         ascent_rate_fsw_per_min);
    };

    auto ok = [&](double hold_time) {
        return dfs_after_hold(hold_time) <= next_depth_fsw + 1e-9;
    };

    // FIRSTM: entering TIME already fails FRSP7 → do not shorten.
    if (!ok(naive_hold_time_min))
        return naive_hold_time_min;
    if (ok(0.0))
        return 0.0;

    // Same 0.1 / 0.005 grid as the flowchart. DFS is monotonic in hold
    // time (longer hold, DFS shallower), so binary search on the 0.1 min
    // decrements from naive matches the linear TIME−=0.1 loop, then the
    // IDFS TIME+=0.005 climb from the first failing 0.1 step.
    const int maxk = (int)std::floor(naive_hold_time_min / kFig29CoarseMin + 1e-12);
    int lo = 0;          // naive - lo*0.1 is safe
    int hi = maxk + 1;   // first k that fails, or past 0
    while (lo + 1 < hi) {
        const int mid = lo + (hi - lo) / 2;
        const double t = std::max(naive_hold_time_min - (double)mid * kFig29CoarseMin, 0.0);
        if (ok(t))
            lo = mid;
        else
            hi = mid;
    }
    const double last_safe =
        std::max(naive_hold_time_min - (double)lo * kFig29CoarseMin, 0.0);
    const double first_fail = std::max(last_safe - kFig29CoarseMin, 0.0);
    double t = first_fail;
    for (int i = 0; i < 40; ++i) {
        t += kFig29FineMin;
        if (t >= last_safe)
            return last_safe;
        if (ok(t))
            return t;
    }
    return last_safe;
}

// TR 10-09 Figure 16 ("Round Time Up"). STIM7 TIME in; occupancy for
// UPDT7 and the printed table cell out. TT is travel time to this stop.
// First air / first O2 stops skip the TIME+=TT / TIME−=TT pair (Table 1
// TTIS exception; Figure 16's FRSTOP).
struct RoundedStopTime {
    double occupancy_min = 0.0;
    double printed_min = 0.0;
};

inline RoundedStopTime round_time_up(const ParameterSet& params, double time_min,
                                      double travel_min, bool skip_travel_in_stop) {
    RoundedStopTime out;
    if (time_min <= 0.0)
        return out;
    double t = time_min;
    const bool add_tt = params.ttis && !skip_travel_in_stop;
    if (add_tt)
        t += travel_min;
    if (params.round_stop_time_up)
        t = std::ceil(t - 1e-9);
    out.printed_min = t;
    if (add_tt)
        t -= travel_min;
    out.occupancy_min = t;
    return out;
}

// A single decompression stop. hold_time_min is occupancy — actual time
// at CDEPTH that feeds UPDT7, excluding travel (TR 18-05 Table 14).
// printed_time_min is the Figure 16 table cell: equal to occupancy when
// TTIS=F or FRSTOP; ceil(TIME+TT) on later TTIS=T stops.
struct ScheduleStop {
    double depth_fsw = 0.0;
    double hold_time_min = 0.0;
    double printed_time_min = 0.0;
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
inline RoundedStopTime run_o2_air_cycling_hold(const ParameterSet& base_params,
                                       std::vector<CompartmentState>& state,
                                       O2AirCyclingState& cyc, double current_depth_fsw,
                                       double next_depth_fsw, double ascent_rate_fsw_per_min,
                                       const ParameterSet& original_params,
                                       double travel_min = 0.0,
                                       bool skip_travel_in_stop = true) {
    const std::size_t n = state.size();
    const double air_fn2 = base_params.fn2;  // AIR_FN2: the bottom/travel gas's own fn2
    double reported_occupancy = 0.0;
    double reported_printed = 0.0;
    // reported_* is O2-breathing time only -- TR 10-09 p.50: "the in-water
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
                    reported_occupancy += lat;
                    reported_printed += lat;
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
            // Final sub-period: Figure 29 then Figure 16. Occupancy
            // feeds UPDT7; printed is the table cell.
            time = optimize_stop_time(gas_params, state, current_depth_fsw, next_depth_fsw,
                                       targets, time, ascent_rate_fsw_per_min);
            const RoundedStopTime rnd =
                round_time_up(base_params, time, travel_min, skip_travel_in_stop);
            if (rnd.occupancy_min > 0.0) {
                for (std::size_t i = 0; i < n; ++i) {
                    advance_compartment(state[i], amb, gas_params.compartments[i],
                                        rnd.occupancy_min);
                }
                if (!cyc.dead_tim) {
                    reported_occupancy += rnd.occupancy_min;
                    reported_printed += rnd.printed_min;
                }
                cyc.rtime -= rnd.occupancy_min;
            }
            break;
        }

        // Capped by RTIME: consume the whole remaining period on the
        // active gas, then loop to switch gas and continue.
        if (time > 0.0) {
            for (std::size_t i = 0; i < n; ++i) {
                advance_compartment(state[i], amb, gas_params.compartments[i], time);
            }
            if (!cyc.dead_tim) {
                reported_occupancy += time;
                reported_printed += time;
            }
        }
        cyc.rtime = 0.0;
    }

    return RoundedStopTime{reported_occupancy, reported_printed};
}

// Ascent half of generate_schedule(): from an already-loaded tissue
// state at current_depth_fsw, walk to the surface. IAD is recomputed
// after each DINC hop so a bounce that is still on-gassing can pick up
// a deeper first stop than the leave-bottom FRSP7 estimate. Callers
// that have already simulated the descent and bottom (Subsurface's
// planner) use this directly; generate_schedule() uses it after doing
// that work itself. `state` is copied. `params_in` must still carry the
// regime's table SDR values (not already CNDSDR-gated) so apply_gas_leg()
// can restore the real table SDR on an enriched deco gas.
inline Schedule generate_ascent_schedule(const ParameterSet& params_in,
                                          std::vector<CompartmentState> state,
                                          double current_depth_fsw,
                                          double ascent_rate_fsw_per_min,
                                          const Profile& deco_legs) {
    if (ascent_rate_fsw_per_min <= 0.0)
        ascent_rate_fsw_per_min = 30.0;
    ParameterSet params = resolve_open_circuit_compartments(params_in);
    const std::size_t n = params.compartments.size();
    if (state.size() < n)
        return Schedule{};
    if (state.size() > n)
        state.resize(n);

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

    auto ramp_ascend = [&](double from_depth, double to_depth, const ParameterSet& gas_params) {
        if (from_depth <= to_depth)
            return;
        advance_profile_ramp(state, gas_params, from_depth, to_depth, ascent_rate_fsw_per_min);
    };

    Schedule schedule;
    const double dinc = params.stop_depth_increment_fsw;
    O2AirCyclingState cyc;
    cyc.strto2 = params.o2_cycling_enabled;
    double arrival_travel_min = 0.0;
    bool seen_o2_stop = false;

    // generate_schedule() always starts on an exact DINC multiple (100, 90,
    // …, 20). Callers that pass a pressure-derived depth (e.g. 100.3 fsw
    // from 100 ft of 1.030 kg/L water) would otherwise step 100.3, 90.3,
    // …, 20.3, 10.3 and miss last_stop_depth_fsw. Snap onto the 0-aligned
    // grid; never snap shallower than the last stop while still deeper
    // than it.
    auto snap_stop = [&](double d) {
        if (d <= 0.0 || dinc <= 0.0)
            return 0.0;
        double g = std::floor(d / dinc + 1e-9) * dinc;
        if (g + 1e-9 < params.last_stop_depth_fsw && g > 0.0)
            g = params.last_stop_depth_fsw;
        return g;
    };

    for (int guard = 0; guard < 500 && current_depth_fsw > 0.0; ++guard) {
        apply_due_legs(current_depth_fsw);

        std::vector<double> tensions(n);
        for (std::size_t i = 0; i < n; ++i) tensions[i] = state[i].tension_fsw;

        const double entering_iad = find_ceiling_fsw(params, tensions, current_depth_fsw);
        const double ceiling = snap_stop(find_first_stop_depth_fsw(
            params, state, current_depth_fsw, entering_iad, ascent_rate_fsw_per_min));
        if (ceiling + 1e-9 < current_depth_fsw) {
            // Figure 10 Set First Stop jumps to DFS when the gap is the
            // leave-bottom first travel. Figure 12 hops one DINC when
            // CDEPTH − max(FDEPTH, LASTOP) ≥ DINC. Bounce dives (300/4,
            // 300/5) have leave-bottom IAD/DFS already at last_stop while
            // fast tissue still on-gasses through 40/30 — keep one-DINC
            // IAD recompute there. Otherwise skip empty DINC levels in
            // one travel so a later hold's next DEPTH can be >1 DINC and
            // Figure 29 (FIRSTM/IDFS) actually runs.
            double hop_to;
            const bool bounce_ongas =
                ceiling <= params.last_stop_depth_fsw + 1e-9 &&
                current_depth_fsw > params.last_stop_depth_fsw + dinc + 1e-9;
            if (bounce_ongas) {
                hop_to = snap_stop(current_depth_fsw) - dinc;
                if (hop_to + 1e-9 < params.last_stop_depth_fsw)
                    hop_to = params.last_stop_depth_fsw;
                hop_to = std::max(hop_to, ceiling);
            } else if (current_depth_fsw > params.last_stop_depth_fsw + 1e-9) {
                hop_to = ceiling;
                if (hop_to + 1e-9 < params.last_stop_depth_fsw)
                    hop_to = params.last_stop_depth_fsw;
            } else {
                hop_to = ceiling;
            }
            const double hop_from = current_depth_fsw;
            ramp_ascend(hop_from, hop_to, params);
            arrival_travel_min = (hop_from - hop_to) / ascent_rate_fsw_per_min;
            current_depth_fsw = hop_to;
            continue;
        }
        if (current_depth_fsw <= 0.0) break;

        AmbientState amb = resolve_ambient_state(params, current_depth_fsw);
        std::vector<double> targets(n);
        for (std::size_t i = 0; i < n; ++i) targets[i] = mptt_value(params, i, current_depth_fsw);

        const double grid_depth = snap_stop(current_depth_fsw);
        double next_depth = (grid_depth <= params.last_stop_depth_fsw + 1e-9)
                                ? 0.0
                                : std::max(grid_depth - dinc, 0.0);

        const bool this_is_o2 =
            params.o2_cycling_enabled && current_depth_fsw <= params.o2ceil_fsw + 1e-9;
        // Table 1 TTIS: travel is omitted from the first air stop and the
        // first O2 stop. schedule.stops.empty() is the first air (or only)
        // stop of the ascent.
        const bool skip_tt = schedule.stops.empty() || (this_is_o2 && !seen_o2_stop);

        RoundedStopTime rnd;
        if (this_is_o2) {
            rnd = run_o2_air_cycling_hold(params, state, cyc, current_depth_fsw, next_depth,
                                          ascent_rate_fsw_per_min, params_in,
                                          arrival_travel_min, skip_tt);
        } else {
            double time = required_stop_time_all(params, tensions, amb, targets);
            if (!std::isfinite(time))
                break;
            // Skip-empty: STIM7 TIME is sized for one DINC. After that
            // hold, FRSP7 DFS may already be shallower than current−DINC.
            // Point DEPTH at that DFS so Figure 28's hop>DINC gate lets
            // Figure 29 run. One-DINC stays one-DINC when DFS equals the
            // STIM7 target. Do not skip through last_stop on a bounce
            // whose IAD can still rise (same guard as the travel hop).
            if (time > 0.0 && next_depth > 0.0) {
                std::vector<CompartmentState> trial = state;
                for (std::size_t i = 0; i < n; ++i)
                    advance_compartment(trial[i], amb, params.compartments[i], time);
                std::vector<double> trial_t(n);
                for (std::size_t i = 0; i < n; ++i) trial_t[i] = trial[i].tension_fsw;
                const double trial_iad =
                    find_ceiling_fsw(params, trial_t, current_depth_fsw);
                const double dfs = snap_stop(find_first_stop_depth_fsw(
                    params, trial, current_depth_fsw, trial_iad, ascent_rate_fsw_per_min));
                if (dfs + 1e-9 < next_depth) {
                    const bool bounce_ongas =
                        dfs <= params.last_stop_depth_fsw + 1e-9 &&
                        current_depth_fsw > params.last_stop_depth_fsw + dinc + 1e-9;
                    if (!bounce_ongas)
                        next_depth = std::max(dfs, 0.0);
                }
            }
            time = optimize_stop_time(params, state, current_depth_fsw, next_depth, targets,
                                      time, ascent_rate_fsw_per_min);
            rnd = round_time_up(params, time, arrival_travel_min, skip_tt);
            if (rnd.occupancy_min > 0.0) {
                for (std::size_t i = 0; i < n; ++i)
                    advance_compartment(state[i], amb, params.compartments[i], rnd.occupancy_min);
            }
        }
        if (rnd.printed_min > 0.0 || rnd.occupancy_min > 0.0) {
            schedule.stops.push_back(
                {current_depth_fsw, rnd.occupancy_min, rnd.printed_min});
            schedule.total_stop_time_min += rnd.occupancy_min;
            if (this_is_o2)
                seen_o2_stop = true;
        }

        const double hop_from = current_depth_fsw;
        if (params.o2_cycling_enabled && cyc.o2strtd) {
            DiveSegment cycle_leg;
            cycle_leg.depth_fsw = current_depth_fsw;
            const double cycle_fn2 = cyc.ono2 ? (1.0 - params.o2time_fo2) : params.fn2;
            cycle_leg.gas = GasMix{1.0 - cycle_fn2, 0.0};
            const ParameterSet cycle_params = apply_gas_leg(params, cycle_leg, params_in);
            ramp_ascend(hop_from, next_depth, cycle_params);
            if (!params.omit_trvl)
                cyc.rtime -= (hop_from - next_depth) / ascent_rate_fsw_per_min;
        } else {
            ramp_ascend(hop_from, next_depth, params);
        }
        arrival_travel_min = (hop_from - next_depth) / ascent_rate_fsw_per_min;
        current_depth_fsw = next_depth;
    }

    return schedule;
}

// Generates a greedy decompression schedule: descend to max_depth_fsw
// (ramping at descent_rate_fsw_per_min), hold at max_depth_fsw for the
// remainder of bottom_time_min (see the bottom-hold note below), then
// skip empty DINC levels in one travel except on bounce dives whose
// leave-bottom IAD is already last_stop (those still hop one DINC so
// IAD can rise — VVal-79 300/4, 300/5). Holds use Figure 29 FIRSTM/IDFS
// when the next stop is more than one DINC shallower. Live ramps use
// closed-form UPDT7 chops of duration ≤ STIME when stime_min > 0. Built from this
// file's/compartment.h's primitives (resolve_ambient_state,
// advance_compartment[_ramp], find_ceiling_fsw,
// required_stop_time_all) plus Figure 9/10/12/29 control flow.
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
    ParameterSet params = resolve_open_circuit_compartments(params_in);
    const std::size_t n = params.compartments.size();
    std::vector<CompartmentState> state(n);

    // Initial tissue loading: saturated on AIR at the surface, computed
    // through the model's own arterial-tension formula — see
    // air_saturation_tension_fsw() and TR 10-09 Appendix B p.B-1.
    {
        const double initial_tension_fsw = air_saturation_tension_fsw(params);
        for (auto& s : state) s.tension_fsw = initial_tension_fsw;
    }

    // Descent: ramp from the surface to max_depth_fsw. STIME>0 chops this
    // into closed-form UPDT7 pieces of duration ≤ STIME; otherwise one ramp.
    const double descent_duration_min = max_depth_fsw / descent_rate_fsw_per_min;
    advance_profile_ramp(state, params, 0.0, max_depth_fsw, descent_rate_fsw_per_min);

    // Bottom hold. bottom_time_min is the diver-facing "Bottom Time" —
    // total elapsed time from leaving the surface to leaving the bottom
    // (US Navy Diving Manual Rev 7 §9-3.2). The actual hold at
    // max_depth_fsw is therefore bottom_time_min minus the descent time.
    const double hold_duration_min = std::max(bottom_time_min - descent_duration_min, 0.0);
    {
        AmbientState amb = resolve_ambient_state(params, max_depth_fsw);
        for (std::size_t i = 0; i < n; ++i) {
            advance_compartment(state[i], amb, params.compartments[i], hold_duration_min);
        }
    }

    return generate_ascent_schedule(params_in, std::move(state), max_depth_fsw,
                                    ascent_rate_fsw_per_min, deco_legs);
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
