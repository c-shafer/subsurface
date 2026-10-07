// libdeco/params/xval_he_9_040.h
//
// XVal-He-9_040: 1.3 atm PO2 He-O2 (CCR) Thalmann Algorithm parameter
// set, modified from XVal-He-9_023 to support no-stop limits matching
// the U.S. Navy Diving Manual's 1.3 atm PO2 He-O2 tables. 5
// compartments — NOT a simple parallel variant of XVal-He-8_040's 3
// (two of the five share a 20-min half-time but different SDR: 2 vs
// 0.67 — these are genuinely distinct compartments, not a duplicate).
//
// Source: Doolette DJ, Murphy RJ, Gerth WA. NEDU TR 18-05 (2018),
// DTIC accession AD1215316. PDF supplied by the user; page numbers
// below refer to that PDF's own printed page numbers. See
// xval_he_8_040.h for the shared-parameter provenance notes (PACO2,
// PH2O, PVCO2, PVO2, AMBAO2, PBOVP, sPBOVP, surface_pressure_fsw,
// last_stop_depth_fsw, stop_depth_increment_fsw) — identical source
// and values for this regime.
//
// Field-by-field provenance specific to this file:
//   half_time_min, sdr, M0 (the 10 fsw MPTT row) — Table 16, p.24.
//   MPTT table (all rows, 10-320 fsw)             — Appendix A "XVAL-HE-9_040
//                                                    (FSW)", p.A-8 (PDF p.43).
//                                                    Transcribed programmatically
//                                                    from pdftotext output, then
//                                                    visually cross-checked
//                                                    against a 400 DPI render of
//                                                    the same page — exact match,
//                                                    see DEVLOG.md.
//
// NOT yet extracted: see xval_he_8_040.h's "NOT yet extracted" note —
// applies identically here (TR 19-05 ascent/descent conventions not
// yet confirmed to match; air-break/gas-switch bookkeeping not wired
// into ParameterSet).
#pragma once

#include "deco/eldcm.h"

namespace deco::params {

inline ParameterSet xval_he_9_040() {
    ParameterSet p;
    p.name = "XVal-He-9_040";
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

    // Table 16, TR 18-05 p.24: half-times [10, 20, 20, 120, 200] min,
    // SDR [1, 2, 0.67, 1, 1].
    p.compartments = {
        {10.0, 1.0},
        {20.0, 2.0},
        {20.0, 0.67},
        {120.0, 1.0},
        {200.0, 1.0},
    };

    // Appendix A "XVAL-HE-9_040 (FSW)", TR 18-05 p.A-8 (PDF p.43).
    // One vector per compartment, indexed the same as p.compartments above.
    p.mptt.per_compartment = {
      {
        {10.0, 85.0}, {20.0, 95.0}, {30.0, 105.0}, {40.0, 115.0},
        {50.0, 125.0}, {60.0, 135.0}, {70.0, 145.0}, {80.0, 155.0},
        {90.0, 165.0}, {100.0, 175.0}, {110.0, 185.0}, {120.0, 195.0},
        {130.0, 205.0}, {140.0, 215.0}, {150.0, 225.0}, {160.0, 235.0},
        {170.0, 245.0}, {180.0, 255.0}, {190.0, 265.0}, {200.0, 275.0},
        {210.0, 285.0}, {220.0, 295.0}, {230.0, 305.0}, {240.0, 315.0},
        {250.0, 325.0}, {260.0, 335.0}, {270.0, 345.0}, {280.0, 355.0},
        {290.0, 365.0}, {300.0, 375.0}, {310.0, 385.0}, {320.0, 395.0},
      },
      {
        {10.0, 64.0}, {20.0, 74.0}, {30.0, 84.0}, {40.0, 94.0},
        {50.0, 104.0}, {60.0, 114.0}, {70.0, 124.0}, {80.0, 134.0},
        {90.0, 144.0}, {100.0, 154.0}, {110.0, 164.0}, {120.0, 174.0},
        {130.0, 184.0}, {140.0, 194.0}, {150.0, 204.0}, {160.0, 214.0},
        {170.0, 224.0}, {180.0, 234.0}, {190.0, 244.0}, {200.0, 254.0},
        {210.0, 264.0}, {220.0, 274.0}, {230.0, 284.0}, {240.0, 294.0},
        {250.0, 304.0}, {260.0, 314.0}, {270.0, 324.0}, {280.0, 334.0},
        {290.0, 344.0}, {300.0, 354.0}, {310.0, 364.0}, {320.0, 374.0},
      },
      {
        {10.0, 83.0}, {20.0, 93.0}, {30.0, 103.0}, {40.0, 113.0},
        {50.0, 123.0}, {60.0, 133.0}, {70.0, 143.0}, {80.0, 153.0},
        {90.0, 163.0}, {100.0, 173.0}, {110.0, 183.0}, {120.0, 193.0},
        {130.0, 203.0}, {140.0, 213.0}, {150.0, 223.0}, {160.0, 233.0},
        {170.0, 243.0}, {180.0, 253.0}, {190.0, 263.0}, {200.0, 273.0},
        {210.0, 283.0}, {220.0, 293.0}, {230.0, 303.0}, {240.0, 313.0},
        {250.0, 323.0}, {260.0, 333.0}, {270.0, 343.0}, {280.0, 353.0},
        {290.0, 363.0}, {300.0, 373.0}, {310.0, 383.0}, {320.0, 393.0},
      },
      {
        {10.0, 41.731}, {20.0, 61.731}, {30.0, 81.731}, {40.0, 101.731},
        {50.0, 121.731}, {60.0, 141.731}, {70.0, 161.731}, {80.0, 181.731},
        {90.0, 201.731}, {100.0, 221.731}, {110.0, 241.731}, {120.0, 261.731},
        {130.0, 281.731}, {140.0, 301.731}, {150.0, 321.731}, {160.0, 341.731},
        {170.0, 361.731}, {180.0, 381.731}, {190.0, 401.731}, {200.0, 421.731},
        {210.0, 441.731}, {220.0, 461.731}, {230.0, 481.731}, {240.0, 501.731},
        {250.0, 521.731}, {260.0, 541.731}, {270.0, 561.731}, {280.0, 581.731},
        {290.0, 601.731}, {300.0, 621.731}, {310.0, 641.731}, {320.0, 661.731},
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
