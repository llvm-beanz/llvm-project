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
