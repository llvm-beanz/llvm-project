# Agent Thoughts: Implementing QuadReadLaneAt for HLSL

## Issue
GitHub issue #99174 requested implementation of `QuadReadLaneAt` HLSL intrinsic.

## Understanding the Feature
`QuadReadLaneAt(T sourceValue, uint quadLaneID)` reads a value from a specific lane
(0-3) in the current quad. It maps to:
- DirectX: DXIL opcode 122 (`quadReadLaneAt`)
- SPIRV: `OpGroupNonUniformQuadBroadcast` (opcode 365)

## Implementation Approach

### 1. Created offload-test-suite test FIRST
Following the instructions, I created the offload test before any Clang/LLVM changes.
The test validates behavior with DXC (the reference compiler) using 4-thread quads
reading from each of the 4 lane indices with int, uint, and float types.

Key insight: With `numthreads(2,2,1)`, thread `(x,y,0)` is at quad lane `y*2+x`,
making the thread index equal to the quad lane index for simple verification.

The test passed with DXC confirming the expected behavior.

### 2. LLVM IR Intrinsics (IntrinsicsDirectX.td / IntrinsicsSPIRV.td)
Added `int_dx_quad_read_lane_at` and `int_spv_quad_read_lane_at` following the
`wave_readlane` pattern since both take `(any_ty, i32)` arguments.
Used `IntrTriviallyScalarizable` for DX (needed for scalarization pass) but not for SPV.

### 3. DXIL Backend (DXIL.td)
Added `QuadReadLaneAt` op mapping to DXIL opcode 122 with `OverloadTy + Int32Ty`
arguments, all_stages, all numeric overloads (half/float/double/i16/i32/i64).

### 4. DirectX TTI and Shader Flags
- Added `dx_quad_read_lane_at` to `isTargetIntrinsicWithScalarOpAtArg` with
  `ScalarOpdIdx == 1` so the index arg doesn't get scalarized when the value arg does.
- Added to `checkWaveOps()` to set the wave ops shader flag.

### 5. SPIRV Backend
- Added `OpGroupNonUniformQuadBroadcast` (opcode 365) to `SPIRVInstrInfo.td` using
  the existing `OpGroupNU4` class pattern (before the existing QuadSwap at 366).
- Added capability `GroupNonUniformQuad` for the new opcode in `SPIRVModuleAnalysis.cpp`.
- Reused `selectWaveOpInst` in `SPIRVInstructionSelector.cpp` - it appends operands
  from index 2 onwards, which correctly passes `(scope=Subgroup, value, index)` to
  the instruction as required by the SPIRV spec.

### 6. Clang Frontend
- `HLSLIntrinsics.td`: Added `hlsl_quad_read_lane_at` as `HLSLTwoArgBuiltin` with
  `[Varying, UIntTy]` args and `AllNumericTypes` (no bool, consistent with QuadReadAcrossX).
- `Builtins.td`: Registered `HLSLQuadReadLaneAt` builtin.
- `CGHLSLRuntime.h`: Added macro registration.
- `CGHLSLBuiltins.cpp`: Added codegen case emitting the intrinsic call.
- `SemaHLSL.cpp`: Added sema checks: arg count, index must be integer, value must be
  non-bool scalar/vector.

## Key Design Decisions

1. **No bool support**: QuadReadLaneAt rejects bool types (unlike WaveReadLaneAt
   which allows bool). This is consistent with other Quad ops and the HLSL spec.

2. **all_stages for DXIL**: Used `all_stages` matching the existing WaveReadLaneAt
   and QuadOp patterns rather than restricting to specific stages.

3. **Reusing selectWaveOpInst**: Rather than writing a new selector function for
   QuadBroadcast, I reused the generic `selectWaveOpInst` which iterates operands
   from position 2, correctly passing value+index to `OpGroupNonUniformQuadBroadcast`.

4. **SPIRV 1.5 test triple**: Used `spirv1.5-vulkan-unknown` for the SPIRV test to
   match the WaveReadLaneAt precedent (before 1.5 Index must be constant).

## Testing Strategy
- offload-test-suite: End-to-end DXC + Vulkan test (int, uint, float scalars, 4 lanes)
- offload-test-suite: End-to-end Clang + Vulkan test (same test, passes with our impl)
- clang codegen test: All numeric types, scalar + vector, DX + SPIRV backends
- clang sema test: Error cases (wrong arg count, wrong types, bool rejection)
- DXIL backend test: Scalar and vector lowering to dx.op.quadReadLaneAt.* opcode 122
- SPIRV backend test: Lowering to OpGroupNonUniformQuadBroadcast
- DXIL shader flags test: Wave ops flag set for quad_read_lane_at

## Rubber Duck Critique
The rubber duck review caught two important integration points I had initially missed:
- `DirectXTargetTransformInfo.cpp`: needed for correct scalarization (index arg stays scalar)
- `DXILShaderFlags.cpp`: needed to set wave ops flag
Both were added to the implementation.

---

# Agent Thoughts: Expanding QuadReadLaneAt Test Coverage

## Issue
The initial QuadReadLaneAt implementation lacked tests for: boolean types, 16-bit
types, 64-bit types, and user-defined structures. The offload-test-suite had zero
QuadReadLaneAt tests.

## Phase 1: Adding Bool Support

### Problem
The initial implementation explicitly rejected booleans via `CheckNotBoolScalarOrVector`
in `SemaHLSL.cpp`, and `HLSLIntrinsics.td` used `AllNumericTypes` (excluding bool).
However, DXC accepts bool types for `QuadReadLaneAt`.

### Changes Made
1. **`HLSLIntrinsics.td`**: Changed `VaryingTypes = AllNumericTypes` → `AllTypesWithBool`
2. **`SemaHLSL.cpp`**: Removed `CheckNotBoolScalarOrVector` call from QuadReadLaneAt
3. **`QuadReadLaneAt-errors.hlsl`**: Removed bool error test cases
4. **`DXIL.td`**: Added `Int1Ty` to QuadReadLaneAt overloads (following WaveReadLaneAt)
5. **`DirectX/QuadReadLaneAt.ll`**: Added i1 scalar test
6. **`SPIRV/hlsl-intrinsics/QuadReadLaneAt.ll`**: Added bool test with `OpTypeBool`
7. **`clang/test/CodeGenHLSL/builtins/QuadReadLaneAt.hlsl`**: Added bool codegen test

### Bool FileCheck Note
For bool types, Clang generates a named SSA value `%loadedv` (from `icmp ne i32 %0, 0`)
instead of a numeric value like `%0`. FileCheck's `%[[#]]` pattern only matches numeric
SSA values, so `{{.*}}` must be used for bool patterns in codegen tests.

## Phase 2: Offload Test Suite Coverage

Created 7 new test files in `offload-test-suite/test/WaveOps/`:

### Test Design
All tests use `numthreads(2,2,1)` with `DispatchSize: [1,1,1]` creating one quad of
4 threads. Thread `(x,y,0)` is at quad lane index `y*2+x`. Most tests read from quad
lane 2 (thread at (0,1,0)) whose input data is `{9, 10, 11, 12}`, so all threads
receive `{9, 10, 11, 12}` — a non-trivial constant lane index validates cross-lane reads.

### Tests Created
- **QuadReadLaneAt.32.test**: int/uint/float x scalar/vec2/vec3/vec4 from lane 2
- **QuadReadLaneAt.fp16.test**: half x scalar/vec2/vec3/vec4 (`REQUIRES: Half`)
- **QuadReadLaneAt.fp64.test**: double x scalar/vec2/vec3/vec4 (`REQUIRES: Double`)
- **QuadReadLaneAt.int16.test**: int16/uint16 x scalar/vec2/vec3/vec4 (`REQUIRES: Half, Int16`)
- **QuadReadLaneAt.int64.test**: int64/uint64 x scalar/vec2/vec3/vec4 (`REQUIRES: Int64`)
- **QuadReadLaneAt.Bool.test**: bool4 with constant lane 2 (passes with both DXC and Clang)
- **QuadReadLaneAt.udt.test**: struct member access with dynamic own-lane index

### Key Discovery: Bool Works with Clang
The Bool test initially had `# XFAIL: Clang` (following WaveReadLaneAt.Bool.test
pattern for bug #140824). However, QuadReadLaneAt.Bool.test passes with Clang
(XPASS), so the annotation was removed. The bool4 SPIRV path for QuadReadLaneAt
works correctly unlike WaveReadLaneAt.Bool.

### Dynamic Index Coverage
The `udt.test` uses `QuadReadLaneAt(v.member, index)` where `index = dtid.y * 2 + dtid.x`
(a dynamic value), validating that dynamic lane indices work. Each thread reads from
its own lane so output equals input.

### Verified Results
All 7 tests pass with both:
- `check-hlsl-vk` (DXC -> DXIL -> DirectX 12)
- `check-hlsl-clang-vk` (Clang -> SPIRV -> Vulkan)
