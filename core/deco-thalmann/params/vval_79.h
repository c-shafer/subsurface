// libdeco/params/vval_79.h
//
// VVal-79: open-circuit air Thalmann Algorithm parameter set, used for the U.S.
// Navy Diving Manual's integrated air decompression tables since Revision 6.
// 9 compartments. This is this library's first non-CPO2 (open-circuit,
// constant-fraction) real parameter set.
//
// Source: Gerth WA, Doolette DJ. NEDU TR 12-01 (Mar 2012), "VVal-79 Maximum
// Permissible Tissue Tension Table for Thalmann Algorithm Support of Air
// Diving." DTIC accession ADA561928. Fetched via archive.org
// (archive.org/download/DTIC_ADA561928/DTIC_ADA561928.pdf) after DTIC's own
// mirror rate-limited — see sources/NEDU-TR-12-01_ADA561928.pdf. Page numbers
// below refer to that PDF's own printed page numbers.
//
// Field-by-field provenance:
//   half_time_min                      — Table 3, p.16 / Table B.1 column
//                                         headers, p.B-1: 5, 10, 20, 40, 80,
//                                         120, 160, 200, 240 min (9
//                                         compartments — more than any He-O2
//                                         XVal regime in this library).
//   sdr                                — Table B.1 column headers, p.B-1:
//                                         0.70 uniformly for all 9
//                                         compartments (unlike the He-O2
//                                         regimes' per-compartment values).
//                                         NOTE: this is the *table* SDR —
//                                         resolve_open_circuit_compartments()
//                                         (eldcm.h) applies the CNDSDR_FO2
//                                         gate at schedule-generation time,
//                                         which forces sdr back to 1.0 for a
//                                         straight-air profile (fn2 = 0.79 >
//                                         1 - cndsdr_fo2 = 0.20) — the 0.70
//                                         value only actually applies when
//                                         breathing an enriched (lower FN2)
//                                         gas, e.g. the AIR/O2 decompression
//                                         procedure this same report also
//                                         covers, which this library does not
//                                         model (single fixed fn2 per
//                                         profile).
//   workman (M0, slope)                — Table 3, p.16 ("Intercept and Slope
//                                         Values for VVal-79 Maximum
//                                         Permissible Tissue Tensions"): slope
//                                         a_i = 1 for every compartment,
//                                         confirmed independently against
//                                         Table B.1's full 30-row point table
//                                         (p.B-1 — every row is exactly +10
//                                         fsw per +10 fsw of depth, for every
//                                         compartment, confirmed by scanning
//                                         the full table, not just two rows).
//                                         M0 values (MPTTi,0, fsw): 99.3,
//                                         87.7, 78.0, 56.0, 48.5, 45.5, 44.5,
//                                         44.0, 43.5 — match Table B.1's own
//                                         10 fsw row exactly, confirming the
//                                         same row-offset/SetM convention
//                                         already implemented in mptt_value()
//                                         (Table B.1's own footnote a, p.B-1:
//                                         "MPTT values in each row are used...
//                                         to assess the time at the
//                                         corresponding stop depth required
//                                         before ascent to the next shallower
//                                         stop depth. Thus, tabulated MPTT
//                                         values for the 10 fsw stop depth
//                                         are the 'surfacing values'... A 20
//                                         fsw last allowed decompression stop
//                                         is implemented by replacing the
//                                         tabulated MPTT values for the 20
//                                         fsw stop depth with those for the
//                                         10 fsw stop depth" — this is a
//                                         direct, explicit, independent
//                                         confirmation of both conventions
//                                         from a second source).
//   PACO2/PH2O/PVCO2/PVO2/AMBAO2       — Table B.2, p.B-2 ("Table of VVal-79
//                                         Global Parameters"). Identical
//                                         values to every He-O2 XVal regime
//                                         in this library (1.50/0.00/2.30/
//                                         2.00/0.00 fsw).
//   pbovp_fsw = 10.00, s_pbovp_fsw=0.00 — Table B.2, p.B-2. NOTE: pbovp_fsw
//                                         differs from every He-O2 regime
//                                         (0.0) — this is a real, cited
//                                         difference, not a copy-paste
//                                         holdover.
//   cndsdr_fo2 = 0.80                  — Table B.2, p.B-2. Used by
//                                         resolve_open_circuit_compartments()
//                                         — see the sdr note above.
//   fn2 = 0.79                         — air's N2 fraction; not separately
//                                         tabulated in TR 12-01 (implicit in
//                                         "air diving"), standard value used
//                                         throughout this project (see
//                                         DEVLOG.md, "what is PAN2" — same
//                                         0.79 used for air_saturation_tension_fsw()).
//   last_stop_depth_fsw = 20.0          — Table B.1 footnote a, p.B-1 (quoted
//                                         above).
//   stop_depth_increment_fsw = 10.0     — Table B.1's own row spacing, p.B-1.
//   lst_domode = 1                      — Table B.2, p.B-2. Last-stop TIME
//                                         assumes instantaneous ascent from
//                                         20 fsw to the surface (TR 10-09
//                                         Table 1). Figure 29 washout is not
//                                         applied on that leg.
//   surface_pressure_fsw = 33.0         — NOT independently re-stated in this
//                                         report; inherited from TR 10-09/TR
//                                         18-05's confirmed universal U.S.
//                                         Navy convention ("33 fsw/atm") —
//                                         flagged as inherited, not
//                                         re-confirmed against this specific
//                                         document.
//   round_stop_time_up = true (RNDUPD)  — Figure 16; Appendix D whole-minute
//                                         cells. Table B.2 lists TTIS=TRUE
//                                         and OMIT_TRVL=TRUE (p.B-2).
//   ttis = true                         — Table B.2. Figure 16 adds travel
//                                         before ceil except first air/first O2.
//   omit_trvl = true                    — Table B.2. Travel omitted from
//                                         ONO2/ONAIR elapsed (cycling RTIME).
//   stime_min = 0.2                     — Table B.2 STIME=0.2 MIN. Live
//                                         descent/travel UPDT7 chops ≤ STIME.
//
//   o2ceil_fsw = 30.0, o2time_min = 30.0,
//   airtime_min = 5.0, gswlat_min = 0.0,
//   o2time_fo2 = 0.80, gs_dead = true,
//   ab_dead = true                      — Table B.2, p.B-2 (O2CEIL, O2TIME,
//                                         AIRTIME, GSWLAT, O2TIME_FO2,
//                                         GS_DEAD, AB_DEAD). Feed
//                                         generate_schedule()'s TR 10-09
//                                         Figure 13 ("O2-Air Cycling") port —
//                                         see run_o2_air_cycling_hold() in
//                                         eldcm.h. o2_cycling_enabled itself
//                                         defaults to false (this factory
//                                         function does not turn it on) so
//                                         every existing AIR-only caller is
//                                         unaffected; set it explicitly to
//                                         exercise the AIR/O2 procedure.
//
// NOT yet extracted / needed for full fidelity:
//   - RGD_SPRSS (Table B.2) — repetitive-group bookkeeping, out of this
//     library's scope (spec pitfall #4).
//   - Table B.3 (SurDO2 surface-decompression-on-oxygen parameters) — a
//     distinct decompression procedure this library does not model.
#pragma once

#include "deco/eldcm.h"

namespace deco::params {

inline ParameterSet vval_79() {
    ParameterSet p;
    p.name = "VVal-79";
    p.constant_po2 = false;
    p.fn2 = 0.79;

    // Table B.2, TR 12-01 p.B-2.
    p.paco2_fsw = 1.50;
    p.ph2o_fsw = 0.00;
    p.pvco2_fsw = 2.30;
    p.pvo2_fsw = 2.00;
    p.ambao2_fsw = 0.0;
    p.pbovp_fsw = 10.00;
    p.s_pbovp_fsw = 0.00;
    p.cndsdr_fo2 = 0.80;

    p.surface_pressure_fsw = 33.0;
    p.last_stop_depth_fsw = 20.0;
    p.stop_depth_increment_fsw = 10.0;
    p.lst_domode = 1;  // Table B.2, p.B-2: last-stop TIME is instantaneous to surface
    p.ttis = true;      // Table B.2: travel in printed stop time except first air/first O2
    p.omit_trvl = true; // Table B.2: travel omitted from ONO2/ONAIR (cycling RTIME)
    p.stime_min = 0.2;  // Table B.2 STIME=0.2 min; live descent/travel UPDT7 chops

    // O2-Air Cycling (TR 10-09 Figure 13) parameters, Table B.2, TR 12-01
    // p.B-2. o2_cycling_enabled itself is left at its default (false) --
    // see the header comment above.
    p.o2ceil_fsw = 30.0;
    p.o2time_min = 30.0;
    p.airtime_min = 5.0;
    p.gswlat_min = 0.0;
    p.o2time_fo2 = 0.80;
    p.gs_dead = true;
    p.ab_dead = true;

    // Table B.1 column headers, TR 12-01 p.B-1: half-times 5, 10, 20, 40, 80,
    // 120, 160, 200, 240 min, SDR 0.70 uniformly.
    p.compartments = {
        {5.0, 0.70},  {10.0, 0.70},  {20.0, 0.70},  {40.0, 0.70}, {80.0, 0.70},
        {120.0, 0.70}, {160.0, 0.70}, {200.0, 0.70}, {240.0, 0.70},
    };

    // Table 3, TR 12-01 p.16: M0 (MPTTi,0) per compartment, slope = 1 for
    // all. Cross-checked against Table B.1's full 30-row point table
    // (10-300 fsw) — every row confirmed +10 fsw per +10 fsw depth, for
    // every compartment.
    p.workman = {
        {99.3, 1.0}, {87.7, 1.0}, {78.0, 1.0}, {56.0, 1.0}, {48.5, 1.0},
        {45.5, 1.0}, {44.5, 1.0}, {44.0, 1.0}, {43.5, 1.0},
    };

    return p;
}

}  // namespace deco::params
