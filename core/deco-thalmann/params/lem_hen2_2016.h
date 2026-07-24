// libdeco/params/lem_hen2_2016.h
//
// LEM-HeN2_2016 parameter set — a re-optimization of the LEM model on the
// expanded "HeN2_2016" training data (more heliox and fixed-FO2 man-trials
// relevant to surface-supplied He-O2 diving than the legacy he8n25 set).
// Same model structure as LEM-he8n25 (see lem_he8n25.h); re-fitted values.
//
// Source: Gerth WA, Murphy FG, Doolette DJ. "Twenty-First Century
// Surface-Supplied Heliox Decompression Table Development." NEDU Technical
// Report 23-11 (2023), DTIC accession AD1215314. All values transcribed from
// Appendix B (p.B-1 / PDF p.44), cross-checked against the tabulated
// half-times on p.B-2 (e.g. tissue-1 He half-time 10.322 min => TC 14.891,
// confirming ln2*TC). Training data: 2943 profiles / 7683 trials / 340 DCS.
//
// Physiological constants: PTH2O=0, RQ=1, PTCO2=PTO2=PACO2=0 (as legacy
// LEM-he8n25 — TR 23-11 p.6 states LEM was fit with PH2O=0 in all inspired
// gases and tissue).
//
// Solubilities (alphab, alphat) are NOT re-listed in TR 23-11 Appendix B;
// they are physical constants carried over from LEM-he8n25 (TR 02-10 Table 4).
// alphat is 0 for He/O2 in the source; layer B substitutes at_n2 for them
// (see deco/lem.h). GAIN SCALING: same "Gain/10**3" column convention as
// he8n25 — actual gain = tabulated value * 1e3 per minute.
#pragma once

#include "deco/lem.h"

namespace deco::params {

inline LemParams lem_hen2_2016() {
    constexpr double GAIN_SCALE = 1.0e3;  // "Gain/10**3" -> actual = value * 1e3

    LemParams p;
    p.name = "LEM HeN2_2016 (TR 23-11 Appendix B)";

    p.rq = 1.0000000000e+00;
    p.pth2o_atm = 0.0;
    p.ptco2_atm = 0.0;
    p.pto2_atm = 0.0;
    p.paco2_atm = 0.0;

    // Blood Ostwald solubilities carried over from LEM-he8n25 (TR 02-10 Tbl 4).
    p.alphab_o2 = 2.610000e-02;
    p.alphab_n2 = 1.580000e-02;
    p.alphab_he = 1.040000e-02;

    p.surface_pressure_fsw = 33.0;
    p.compartments.resize(3);

    // --- Tissue 1 ---
    {
        LemCompartment& c = p.compartments[0];
        c.tc_o2_min = 1.0000000000e+00;
        c.tc_n2_min = 7.1066028539e+00;
        c.tc_he_min = 1.4891046457e+01;
        c.at_o2 = 0.0;
        c.at_n2 = 7.300000e-02;  // carried over from he8n25; used by layer B
        c.at_he = 0.0;
        c.pset_ata = 9.9000000000e+01;
        c.pxo_atm = 4.5314588828e-01;
        c.thr_atm = 7.8779000000e-01;
        c.gain_per_min = 1.1470964203e-06 * GAIN_SCALE;
    }
    // --- Tissue 2 ---
    {
        LemCompartment& c = p.compartments[1];
        c.tc_o2_min = 2.0606789498e+01;
        c.tc_n2_min = 5.1188690508e+01;
        c.tc_he_min = 3.0855198631e+01;
        c.at_o2 = 0.0;
        c.at_n2 = 7.300000e-02;
        c.at_he = 0.0;
        c.pset_ata = 9.5472914101e-01;  // finite -> O2 tracks dynamically
        c.pxo_atm = -2.7109000000e-02;
        c.thr_atm = 1.9786000000e+00;   // fixed (not fitted) — cmpt 2 dormant
        c.gain_per_min = 2.1611214333e-05 * GAIN_SCALE;
    }
    // --- Tissue 3 ---
    {
        LemCompartment& c = p.compartments[2];
        c.tc_o2_min = 1.0000000000e+00;
        c.tc_n2_min = 2.6772472105e+02;
        c.tc_he_min = 2.7690139813e+02;
        c.at_o2 = 0.0;
        c.at_n2 = 7.300000e-02;
        c.at_he = 0.0;
        c.pset_ata = 9.9000000000e+01;
        c.pxo_atm = 9.9999999999e+09;   // tissue 3 never forms bubbles
        c.thr_atm = -1.5640353796e-01;
        c.gain_per_min = 5.4271127761e-07 * GAIN_SCALE;
    }

    return p;
}

}  // namespace deco::params
