// libdeco/params/xval_he_8_023.h
//
// XVal-He-8_023: 1.3 atm PO2 He-O2 (CCR) Thalmann Algorithm parameter
// set targeting a 2.3% PDCS standard set. 3 compartments. This is the
// original XVal-He-8 fit; XVal-He-8_040/8_050 (already in this
// directory) are the 4.0%/5.0% PDCS re-fits of the same three
// compartments, and XVal-He-9_023 (see xval_he_9_023.h) modifies this
// set's fast/intermediate compartments while retaining its 210-min
// slow compartment unchanged.
//
// Source: Doolette DJ, Murphy RJ, Gerth WA. NEDU TR 18-05 (2018),
// "Thalmann Algorithm Computational Principles and Deterministic
// Decompression Tables for Air/N2-O2 and He-O2 Diving to 300 fsw."
// DTIC accession AD1215316. PDF supplied by the user; page numbers
// below refer to that PDF's own printed page numbers, each confirmed
// by rendering the page at 300-400 DPI (not just pdftotext output) --
// see DEVLOG.md.
//
// Field-by-field provenance:
//   half_time_min, sdr, M0 (the 10 fsw MPTT row) — Table 6, p.13.
//   MPTT table (all rows, 10-320 fsw)              — Appendix A "XVAL-HE-8_023
//                                                     (FSW)", p.A-2. Transcribed
//                                                     programmatically from
//                                                     pdftotext output, then
//                                                     visually cross-checked
//                                                     against a 400 DPI render of
//                                                     the same page -- exact
//                                                     match, see DEVLOG.md.
//   PACO2/PH2O/PVCO2/PVO2/AMBAO2/PBOVP/sPBOVP       — "Other Thalmann Algorithm
//                                                     Parameters and Conventions",
//                                                     p.A-13 (identical source
//                                                     and values as
//                                                     xval_he_8_040.h -- see that
//                                                     file for the full citation).
//   surface_pressure_fsw, last_stop_depth_fsw,
//   stop_depth_increment_fsw                        — same source/values as
//                                                     xval_he_8_040.h.
//   constant_po2 = true, po2_setpoint_atm = 1.3     — regime name/description
//                                                     ("1.3 atm PO2 He-O2"), p.7.
//
// NOT yet extracted: see xval_he_8_040.h's "NOT yet extracted" note --
// applies identically here.
#pragma once

#include "deco/eldcm.h"

namespace deco::params {

inline ParameterSet xval_he_8_023() {
    ParameterSet p;
    p.name = "XVal-He-8_023";
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

    // Table 6, TR 18-05 p.13: half-times [10, 20, 210] min, SDR [1, 0.67, 1].
    p.compartments = {
        {10.0, 1.0},
        {20.0, 0.67},
        {210.0, 1.0},
    };

    // Appendix A "XVAL-HE-8_023 (FSW)", TR 18-05 p.A-2.
    // One vector per compartment, indexed the same as p.compartments above.
    p.mptt.per_compartment = {
      {
        {10.0, 66.016}, {20.0, 76.016}, {30.0, 86.016}, {40.0, 96.016},
        {50.0, 106.016}, {60.0, 116.016}, {70.0, 126.016}, {80.0, 136.016},
        {90.0, 146.016}, {100.0, 156.016}, {110.0, 166.016}, {120.0, 176.016},
        {130.0, 186.016}, {140.0, 196.016}, {150.0, 206.016}, {160.0, 216.016},
        {170.0, 226.016}, {180.0, 236.016}, {190.0, 246.016}, {200.0, 256.016},
        {210.0, 266.016}, {220.0, 276.016}, {230.0, 286.016}, {240.0, 296.016},
        {250.0, 306.016}, {260.0, 316.016}, {270.0, 326.016}, {280.0, 336.016},
        {290.0, 346.016}, {300.0, 356.016}, {310.0, 366.016}, {320.0, 376.016},
      },
      {
        {10.0, 82.825}, {20.0, 92.825}, {30.0, 102.825}, {40.0, 112.825},
        {50.0, 122.825}, {60.0, 132.825}, {70.0, 142.825}, {80.0, 152.825},
        {90.0, 162.825}, {100.0, 172.825}, {110.0, 182.825}, {120.0, 192.825},
        {130.0, 202.825}, {140.0, 212.825}, {150.0, 222.825}, {160.0, 232.825},
        {170.0, 242.825}, {180.0, 252.825}, {190.0, 262.825}, {200.0, 272.825},
        {210.0, 282.825}, {220.0, 292.825}, {230.0, 302.825}, {240.0, 312.825},
        {250.0, 322.825}, {260.0, 332.825}, {270.0, 342.825}, {280.0, 352.825},
        {290.0, 362.825}, {300.0, 372.825}, {310.0, 382.825}, {320.0, 392.825},
      },
      {
        {10.0, 34.165}, {20.0, 44.165}, {30.0, 54.165}, {40.0, 64.165},
        {50.0, 74.165}, {60.0, 84.165}, {70.0, 94.165}, {80.0, 104.165},
        {90.0, 114.165}, {100.0, 124.165}, {110.0, 134.165}, {120.0, 144.165},
        {130.0, 154.165}, {140.0, 164.165}, {150.0, 174.165}, {160.0, 184.165},
        {170.0, 194.165}, {180.0, 204.165}, {190.0, 214.165}, {200.0, 224.165},
        {210.0, 234.165}, {220.0, 244.165}, {230.0, 254.165}, {240.0, 264.165},
        {250.0, 274.165}, {260.0, 284.165}, {270.0, 294.165}, {280.0, 304.165},
        {290.0, 314.165}, {300.0, 324.165}, {310.0, 334.165}, {320.0, 344.165},
      },
    };

    return p;
}

}  // namespace deco::params
