# Vendored libdeco (Thalmann EL-DCM)

This directory is a vendored, unmodified copy of the public headers and
parameter sets of `libdeco`, a standalone C++17 implementation of the
Thalmann Exponential-Linear Decompression Model (EL-DCM) transcribed
from U.S. Navy NEDU technical reports (TR 10-09, TR 18-05, TR 12-01).

Upstream: the `lemdeco` repository, `libdeco/` subdirectory (same
author). Do not edit these files here; fix upstream and re-copy.

Contents:

- `deco/profile.h`, `deco/compartment.h`, `deco/eldcm.h` — the model
  (compartment kinetics, MPTT/ceiling primitives, parameter set type).
- `params/*.h` — cited parameter-set data: `vval_79` (open-circuit
  air, NEDU TR 12-01), `xval_he_8_023`, `xval_he_8_040`,
  `xval_he_9_023`, `xval_he_9_040` (1.3 atm PO2 He-O2 closed-circuit,
  NEDU TR 18-05). The 5.0% PDCS sets are deliberately not vendored
  (disabled upstream).

Subsurface integration lives in `core/thalmann.h` / `core/thalmann.cpp`,
which adapts these primitives to Subsurface's deco-engine contract
(`clear_deco` / `add_segment` / `tissue_tolerance_calc`). The schedule
walker in `core/planner.cpp` is Subsurface's own; libdeco's
`generate_schedule()` is not used.
