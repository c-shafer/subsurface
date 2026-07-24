// libdeco/params/lem_he8n25.h
//
// LEM model parameter set — the "Duke LEMGEN" form calibrated about the
// he8n25 data set (4,669 dives, 242 DCS).
//
// Source: Gerth WA, Johnson TM. "Development and Validation of 1.3 ATA
// PO2-in-He Decompression Tables for the MK 16 MOD 1 UBA." NEDU Technical
// Report 02-10 (2002), DTIC accession ADA407841. All values transcribed
// from Table 4 (p.7 of the report / PDF p.9), verified against a 400 DPI
// render of the value column (in particular TC-N2 compartment 1 is
// 4.739182E+00 min, confirmed E+00 not E+01). Physiological constants
// (PTH2O, RQ, PTCO2, PTO2, PACO2) and blood/tissue solubilities are the
// leading rows of Table 4.
//
// This is the probabilistic LEM scorer's parameter set (see deco/lem.h),
// NOT an EL-DCM/Workman ParameterSet. Native units: atm, min, ml/ml.
//
// GAIN SCALING NOTE: Table 4 tabulates the gains under the column label
// "Gain/10**3 [/min]" — i.e. the printed number is G_i divided by 10^3, so
// the actual gain is (printed value) * 1e3 per minute. That interpretation
// (rather than G_i = printed value) is the one that yields physically
// plausible single-digit-percent P_DCS for MK 16-class He-O2 schedules; it
// is applied below and flagged for validation in DEVLOG.md. If a future
// cross-check against a report-stated P_DCS (e.g. Appendix L profiles)
// contradicts it, change it in this one place.
#pragma once

#include "deco/lem.h"

namespace deco::params {

inline LemParams lem_he8n25() {
    constexpr double MMHG_PER_ATM = 760.0;
    constexpr double GAIN_SCALE = 1.0e3;  // "Gain/10**3" -> actual = value * 1e3

    LemParams p;
    p.name = "LEM he8n25 (Duke LEMGEN, TR 02-10 Table 4)";

    // Global constants (Table 4 leading rows). All the mm-Hg quantities are
    // 0 in this calibrated set, so the arterial equation reduces to
    // p_a,g = f_g * Pamb and p_fix = 0.
    p.rq = 1.000000e+00;                       // Respiratory Quotient
    p.pth2o_atm = 0.0 / MMHG_PER_ATM;          // PTH2O
    p.ptco2_atm = 0.0 / MMHG_PER_ATM;          // PTCO2 = PVCO2
    p.pto2_atm = 0.0 / MMHG_PER_ATM;           // PTO2 = PVO2
    p.paco2_atm = 0.0 / MMHG_PER_ATM;          // PACO2

    // alphab: blood Ostwald solubilities, ml/ml.
    p.alphab_o2 = 2.610000e-02;
    p.alphab_n2 = 1.580000e-02;
    p.alphab_he = 1.040000e-02;

    p.surface_pressure_fsw = 33.0;  // U.S. Navy rounded fsw/atm (matches libdeco)

    // Three compartments. Fields per Table 4, gas order O2/N2/He.
    p.compartments.resize(3);

    // --- Compartment 1 ---
    {
        LemCompartment& c = p.compartments[0];
        c.tc_o2_min = 1.000000e+00;
        c.tc_n2_min = 4.739182e+00;
        c.tc_he_min = 1.507232e+01;
        c.at_o2 = 0.0;
        c.at_n2 = 7.300000e-02;
        c.at_he = 0.0;
        c.pset_ata = 9.900000e+01;   // large -> O2 static in cmpt 1
        c.pxo_atm = 5.789038e-01;
        c.thr_atm = 7.877900e-01;
        c.gain_per_min = 1.458592e-06 * GAIN_SCALE;
    }
    // --- Compartment 2 ---
    {
        LemCompartment& c = p.compartments[1];
        c.tc_o2_min = 2.322581e+01;
        c.tc_n2_min = 4.756701e+01;
        c.tc_he_min = 3.135001e+01;
        c.at_o2 = 0.0;
        c.at_n2 = 7.300000e-02;
        c.at_he = 0.0;
        c.pset_ata = 9.087054e-01;   // finite -> O2 tracks dynamically in cmpt 2
        c.pxo_atm = -2.710900e-02;
        c.thr_atm = 1.978600e+00;
        c.gain_per_min = 2.480900e-05 * GAIN_SCALE;
    }
    // --- Compartment 3 ---
    {
        LemCompartment& c = p.compartments[2];
        c.tc_o2_min = 1.000000e+00;
        c.tc_n2_min = 3.277423e+02;
        c.tc_he_min = 2.907310e+02;
        c.at_o2 = 0.0;
        c.at_n2 = 7.300000e-02;
        c.at_he = 0.0;
        c.pset_ata = 9.900000e+01;   // large -> O2 static in cmpt 3
        c.pxo_atm = 1.000000e+10;    // cmpt 3 never forms bubbles
        c.thr_atm = -1.492947e-01;
        c.gain_per_min = 5.780815e-07 * GAIN_SCALE;
    }

    // dt_min left at LemParams default (1.0) — project decision, see DEVLOG.
    return p;
}

}  // namespace deco::params
