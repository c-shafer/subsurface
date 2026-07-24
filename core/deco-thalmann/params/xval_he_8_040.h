// libdeco/params/xval_he_8_040.h
//
// XVal-He-8_040: 1.3 atm PO2 He-O2 (CCR) Thalmann Algorithm parameter
// set targeting a 4.0% PDCS standard set. 3 compartments.
//
// Source: Doolette DJ, Murphy RJ, Gerth WA. NEDU TR 18-05 (2018),
// "Thalmann Algorithm Computational Principles and Deterministic
// Decompression Tables for Air/N2-O2 and He-O2 Diving to 300 fsw."
// DTIC accession AD1215316. PDF supplied by the user; page numbers
// below refer to that PDF's own printed page numbers.
//
// Field-by-field provenance:
//   half_time_min, sdr, M0 (the 10 fsw MPTT row)  — Table 12, p.21.
//   MPTT table (all rows, 10-320 fsw)              — Appendix A "XVAL-HE-8_040
//                                                     (FSW)", p.A-6 (PDF p.41).
//                                                     Transcribed programmatically
//                                                     from pdftotext output, then
//                                                     visually cross-checked
//                                                     against a 400 DPI render of
//                                                     the same page — exact match,
//                                                     see DEVLOG.md.
//   PACO2/PH2O/PVCO2/PVO2/AMBAO2/PBOVP/sPBOVP       — "Other Thalmann Algorithm
//                                                     Parameters and Conventions",
//                                                     p.A-13 (PDF p.53). These are
//                                                     stated as common to ALL
//                                                     tables in the appendices
//                                                     (i.e. not specific to
//                                                     8_040 vs 9_040).
//   surface_pressure_fsw = 33.0                     — p.7 and p.A-13 footnote:
//                                                     "the Thalmann Algorithm...
//                                                     uses the U.S. Navy rounded
//                                                     conversion factor of
//                                                     33 fsw/atm" (the fitting
//                                                     software's 33.078 fsw/atm
//                                                     was used only to derive
//                                                     the M0 values already
//                                                     baked into the MPTT table
//                                                     above, not by the runtime
//                                                     algorithm itself).
//   last_stop_depth_fsw = 20.0                      — p.A-4: "the last allowed
//                                                     stop is 20 fsw, and... this
//                                                     is handled internally by
//                                                     overwriting the 20 fsw row
//                                                     of the MPTT table with the
//                                                     surfacing values from the
//                                                     10 fsw row" (TR 10-09's SetM
//                                                     routine — NOT YET applied
//                                                     to the table below; a
//                                                     schedule generator must
//                                                     still perform this
//                                                     overwrite, see DEVLOG.md).
//   constant_po2 = true, po2_setpoint_atm = 1.3     — regime name/description
//                                                     ("1.3 atm PO2 He-O2"), p.7.
//   cndsdr_fo2                                      — NOT given in TR 18-05
//                                                     (irrelevant under CPO2 —
//                                                     Figure 32's gate always
//                                                     takes the SDR branch when
//                                                     constant_po2 is true).
//                                                     Left at the ParameterSet
//                                                     default (0.0); does not
//                                                     affect this regime's
//                                                     kinetics.
//   stop_depth_increment_fsw = 10.0                 — the MPTT table's own row
//                                                     spacing, p.A-6.
//
// NOT yet extracted / needed for full fidelity:
//   - Ascent/descent rate conventions matching the ORIGINAL implementation
//     spec's Appendix C golden vectors specifically come from NEDU TR 19-05
//     (not this document) — TR 18-05's own "Conventions" section (p.A-13)
//     gives 60 fsw/min descent / 30 fsw/min ascent for the Appendix
//     decompression tables in THIS report, which is a plausible but
//     NOT YET CONFIRMED match to TR 19-05's man-tested profiles. Do not
//     assume these are the same until TR 19-05 is fetched.
//   - Air-break / gas-switch dead-time bookkeeping constants (BTMAX,
//     TATMAX, O2TIME, AIRTIME, etc., p.A-13) — captured in DEVLOG.md for
//     reference but not wired into ParameterSet (out of Phase 1/2 scope,
//     spec pitfall #4).
#pragma once

#include "deco/eldcm.h"

namespace deco::params {

inline ParameterSet xval_he_8_040() {
    ParameterSet p;
    p.name = "XVal-He-8_040";
    p.constant_po2 = true;
    p.po2_setpoint_atm = 1.3;

    // "Other Thalmann Algorithm Parameters and Conventions", TR 18-05 p.A-13.
    p.paco2_fsw = 1.50;
    p.ph2o_fsw = 0.00;
    p.pvco2_fsw = 2.30;
    p.pvo2_fsw = 2.00;
    p.ambao2_fsw = 0.0;
    p.pbovp_fsw = 0.0;
    p.s_pbovp_fsw = 0.0;

    p.surface_pressure_fsw = 33.0;
    p.last_stop_depth_fsw = 20.0;
    p.stop_depth_increment_fsw = 10.0;
    // round_stop_time_up (RNDUPD=T, TR 18-05 p.A-13) is left at the
    // ParameterSet default (true) — matches this regime's source
    // convention; see ParameterSet::round_stop_time_up.

    // Table 12, TR 18-05 p.21: half-times [10, 20, 200] min, SDR [1, 0.67, 1].
    p.compartments = {
        {10.0, 1.0},
        {20.0, 0.67},
        {200.0, 1.0},
    };

    // Appendix A "XVAL-HE-8_040 (FSW)", TR 18-05 p.A-6 (PDF p.41).
    // One vector per compartment, indexed the same as p.compartments above.
    p.mptt.per_compartment = {
      {
        {10.0, 70.82}, {20.0, 80.82}, {30.0, 90.82}, {40.0, 100.82},
        {50.0, 110.82}, {60.0, 120.82}, {70.0, 130.82}, {80.0, 140.82},
        {90.0, 150.82}, {100.0, 160.82}, {110.0, 170.82}, {120.0, 180.82},
        {130.0, 190.82}, {140.0, 200.82}, {150.0, 210.82}, {160.0, 220.82},
        {170.0, 230.82}, {180.0, 240.82}, {190.0, 250.82}, {200.0, 260.82},
        {210.0, 270.82}, {220.0, 280.82}, {230.0, 290.82}, {240.0, 300.82},
        {250.0, 310.82}, {260.0, 320.82}, {270.0, 330.82}, {280.0, 340.82},
        {290.0, 350.82}, {300.0, 360.82}, {310.0, 370.82}, {320.0, 380.82},
      },
      {
        {10.0, 83.204}, {20.0, 93.204}, {30.0, 103.204}, {40.0, 113.204},
        {50.0, 123.204}, {60.0, 133.204}, {70.0, 143.204}, {80.0, 153.204},
        {90.0, 163.204}, {100.0, 173.204}, {110.0, 183.204}, {120.0, 193.204},
        {130.0, 203.204}, {140.0, 213.204}, {150.0, 223.204}, {160.0, 233.204},
        {170.0, 243.204}, {180.0, 253.204}, {190.0, 263.204}, {200.0, 273.204},
        {210.0, 283.204}, {220.0, 293.204}, {230.0, 303.204}, {240.0, 313.204},
        {250.0, 323.204}, {260.0, 333.204}, {270.0, 343.204}, {280.0, 353.204},
        {290.0, 363.204}, {300.0, 373.204}, {310.0, 383.204}, {320.0, 393.204},
      },
      {
        {10.0, 38.274}, {20.0, 50.157}, {30.0, 62.039}, {40.0, 73.921},
        {50.0, 85.803}, {60.0, 97.685}, {70.0, 109.567}, {80.0, 121.449},
        {90.0, 133.331}, {100.0, 145.213}, {110.0, 157.095}, {120.0, 168.977},
        {130.0, 180.859}, {140.0, 192.741}, {150.0, 204.623}, {160.0, 216.506},
        {170.0, 228.388}, {180.0, 240.27}, {190.0, 252.152}, {200.0, 264.034},
        {210.0, 275.916}, {220.0, 287.798}, {230.0, 299.68}, {240.0, 311.562},
        {250.0, 323.444}, {260.0, 335.326}, {270.0, 347.208}, {280.0, 359.09},
        {290.0, 370.972}, {300.0, 382.855}, {310.0, 394.737}, {320.0, 406.619},
      },
    };

    return p;
}

}  // namespace deco::params
