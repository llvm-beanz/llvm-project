// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.GroupNonUniformBroadcast`/`BroadcastFirst` (roadmap L89e)
// convert to `llvm.spv.wave.broadcast`/`llvm.spv.wave.readlane` respectively.
// Both were wholly missing, failing all 336
// `dEQP-VK.subgroups.ballot_broadcast.compute.*` cases with a
// conversion-legalization error.

// `Broadcast` gets its own intrinsic, distinct from `Shuffle`'s
// `llvm.spv.wave.readlane`: "Result is the Value of the invocation
// identified by the id Id" is the same sentence `GroupNonUniformShuffle`'s
// own spec text uses, but `Broadcast` additionally requires Id to be
// dynamically uniform, a guarantee `Shuffle` does not make. Reusing
// `readlane` outright (this test's own original shape) meant that
// guarantee was invisible past this conversion, forcing every downstream
// consumer to treat the result as a possibly-divergent per-lane gather --
// which is what made `ballot_broadcast`'s own CTS shape (one static call
// per possible source lane) blow up into `O(WaveSize^2)` IR and hang
// `PromoteMem2Reg`/SROA for the `_requiredsubgroupsize{64,128}` cases (see
// roadmap `L148`). `llvm.spv.wave.broadcast` lets `feme::cpu::WaveLowering
// .cpp`'s `lowerBroadcast` compute the always-uniform result in `O(1)`
// instead.

// CHECK-LABEL: llvm.func @broadcast_scalar
// CHECK: %[[ID:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: %[[RES:.*]] = llvm.call_intrinsic "llvm.spv.wave.broadcast"(%arg0, %[[ID]]) : (f32, i32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @broadcast_scalar(%v : f32) -> f32 "None" {
    %id = spirv.Constant 2 : i32
    %0 = spirv.GroupNonUniformBroadcast <Subgroup> %v, %id : f32, i32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// The `i1` operand shape, which the group's own `bool`/`bvec*` cases exercise
// and which the CTS diagnostic for this milestone reported verbatim
// (`(i1, i32) -> i1`). It needs nothing special: `llvm.spv.wave.broadcast`
// already carries `i1` through `SIMDizePass`/`WaveLowering.cpp`.

// CHECK-LABEL: llvm.func @broadcast_bool
// CHECK: llvm.call_intrinsic "llvm.spv.wave.broadcast"({{.*}}) : (i1, i32) -> i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @broadcast_bool(%v : i1) -> i1 "None" {
    %id = spirv.Constant 0 : i32
    %0 = spirv.GroupNonUniformBroadcast <Subgroup> %v, %id : i1, i32
    spirv.ReturnValue %0 : i1
  }
}

// -----

// A vector operand (the group's `vec*`/`ivec*`/`uvec*` cases). Also the
// dynamically-uniform (non-constant) id shape, which SPIR-V permits from
// version 1.5 on and which the CTS `*_nonconst` cases use -- the lowering is
// identical either way.

// CHECK-LABEL: llvm.func @broadcast_vector_nonconst
// CHECK: llvm.call_intrinsic "llvm.spv.wave.broadcast"(%arg0, %arg1) : (vector<4xi32>, i32) -> vector<4xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.5, [Shader, GroupNonUniform, GroupNonUniformBallot], []>
    attributes {spirv.target_env = #spirv.target_env<#spirv.vce<v1.5, [Shader, GroupNonUniform, GroupNonUniformBallot], []>, #spirv.resource_limits<>>} {
  spirv.func @broadcast_vector_nonconst(%v : vector<4xi32>, %id : i32) -> vector<4xi32> "None" {
    %0 = spirv.GroupNonUniformBroadcast <Subgroup> %v, %id : vector<4xi32>, i32
    spirv.ReturnValue %0 : vector<4xi32>
  }
}

// -----

// `BroadcastFirst` reads from the first *active* invocation, computed as the
// lowest set bit of this subgroup's own active-invocation ballot:
// `ballot(true)` is by definition the mask of currently-active invocations, so
// its lowest set bit is exactly "the active invocation with the lowest id".
// Unlike plain `Broadcast`, this still goes through `llvm.spv.wave.readlane`,
// not the new `llvm.spv.wave.broadcast`: this pattern computes its own lane
// index at runtime (`cttz` of a ballot) rather than being handed one the
// SPIR-V spec already guarantees is uniform, so it does not carry the same
// producer-side guarantee `Broadcast`'s own conversion pattern now exploits.
//
// The ballot is clipped to `gl_SubgroupSize` for the same reason
// `BallotFindLSBConversionPattern` clips it. Unlike that pattern, though,
// `is_zero_poison` must be *false*: `cttz` of an all-zero mask would otherwise
// produce 128 and feed `lowerReadLane` an out-of-range lane index.

// CHECK-LABEL: llvm.func @broadcast_first
// CHECK: %[[TRUE:.*]] = llvm.mlir.constant(true) : i1
// CHECK: %[[BALLOT:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.ballot"(%[[TRUE]]) : (i1) -> vector<4xi32>
// CHECK: %[[BITS:.*]] = llvm.bitcast %[[BALLOT]] : vector<4xi32> to i128
// CHECK: llvm.call_intrinsic "llvm.spv.subgroup.size"() : () -> i32
// CHECK: %[[CLIPPED:.*]] = llvm.and %[[BITS]], %{{.*}} : i128
// CHECK: %[[LSB:.*]] = "llvm.intr.cttz"(%[[CLIPPED]]) <{is_zero_poison = false}> : (i128) -> i128
// CHECK: %[[LANE:.*]] = llvm.trunc %[[LSB]] : i128 to i32
// CHECK: %[[RES:.*]] = llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %[[LANE]]) : (f32, i32) -> f32
// CHECK: llvm.return %[[RES]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @broadcast_first(%v : f32) -> f32 "None" {
    %0 = spirv.GroupNonUniformBroadcastFirst <Subgroup> %v : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// `BroadcastFirst` over a vector operand, and over `i1`.

// CHECK-LABEL: llvm.func @broadcast_first_vector
// CHECK: llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %{{.*}}) : (vector<4xi32>, i32) -> vector<4xi32>
// CHECK-LABEL: llvm.func @broadcast_first_bool
// CHECK: llvm.call_intrinsic "llvm.spv.wave.readlane"(%arg0, %{{.*}}) : (i1, i32) -> i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @broadcast_first_vector(%v : vector<4xi32>) -> vector<4xi32> "None" {
    %0 = spirv.GroupNonUniformBroadcastFirst <Subgroup> %v : vector<4xi32>
    spirv.ReturnValue %0 : vector<4xi32>
  }
  spirv.func @broadcast_first_bool(%v : i1) -> i1 "None" {
    %0 = spirv.GroupNonUniformBroadcastFirst <Subgroup> %v : i1
    spirv.ReturnValue %0 : i1
  }
}
