# Agent Engineering Notes

## Decisions

- Treated `dcl_indexrange` as defining one DXIL signature element spanning the
  declared rows. The element keeps the first register and gathers the source
  semantic indices in row order.
- Rebuilt the signature's register/component lookup after collapsing elements
  so both static and dynamically indexed reads resolve to the renumbered
  element.
- Made static input loads use a row relative to the collapsed element, matching
  the row convention already used by dynamically indexed loads.
- Kept `struct_buf1` checks functional rather than instruction-for-instruction.
  The checks cover handle creation, structured loads, residency checks, stores,
  outputs, and resource metadata without rejecting an extra bit reinterpretation.
- No design-document update was needed because the change implements the
  documented DXBC-to-DXIL translation rather than changing its architecture or
  scope.

## Test Coverage

- Added a DXSA-to-LLVM unit test for element collapse, semantic-index gathering,
  element renumbering, and dynamic input access.
- Converted `indexableinput1`, `indexableinput2`, and `struct_buf1` to complete
  assembly/import/translation FileCheck pipelines and removed their `.ref`
  files.
- Built with the existing assertion-enabled, ccache-backed `build` directory.
- Ran the DXSA translator unit tests, all DXBC translation lit tests, and the
  complete `check-feme` suite.
