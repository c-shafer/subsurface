// libdeco/include/deco/profile.h
//
// Core profile data types shared by the EL-DCM engine, the LEM-NMRI98
// scorer, and the reference Bühlmann implementation.
//
// Unit convention:
//   - depth       : feet of seawater (fsw)
//   - pressure/gas tension: feet of seawater (fsw)
//   - time        : minutes
//   - gas fraction: unitless [0, 1]
// The implementation spec's §5 suggested atm for pressure. Deviation,
// recorded here and in DEVLOG.md: NEDU TR 10-09 Table 1 defines every
// tissue-tension-related quantity (PBOVP, sPBOVP, PVO2, PVCO2, PACO2,
// PH2O, AMBAO2, MPTT values) natively in fsw, not atm — using fsw
// throughout means every numeric parameter extracted from the primary
// sources drops in with zero unit conversion, which is the actual
// transcription-error-reducing property the spec was after. Convert at
// the boundary (e.g. if a caller works in msw/bar/atm) — nothing inside
// libdeco should silently mix unit systems.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace deco {

// Open-circuit gas mixture. For closed-circuit (constant PO2) segments,
// fo2/fhe are not meaningful on their own — see DiveSegment::po2_setpoint.
struct GasMix {
    double fo2 = 0.21;
    double fhe = 0.0;

    double fn2() const { return 1.0 - fo2 - fhe; }
};

// One leg of a dive profile: a depth held (or transited to) for a duration,
// breathing a given gas. If po2_setpoint is set, this segment is a
// closed-circuit constant-PO2 segment and the inert fraction must be
// derived from ambient pressure at each instant (see eldcm.h) rather than
// read directly off `gas`. po2_setpoint is in atm (ATA) — the
// conventional diver-facing unit for a CCR setpoint (e.g. 1.3) — and is
// converted to an fsw partial pressure at the point of use via the
// parameter set's surface_pressure_fsw, matching TR 10-09's
// PAO2 = PO2*SURFP_FSW*(1.0-PH2O/PAMB) - AMBAO2 (Figure 31).
struct DiveSegment {
    double depth_fsw = 0.0;
    double time_min = 0.0;
    GasMix gas;
    std::optional<double> po2_setpoint;
};

using Profile = std::vector<DiveSegment>;

}  // namespace deco
