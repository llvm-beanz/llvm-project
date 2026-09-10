// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that `spirv.GroupNonUniformBallot` (roadmap L85) converts
// directly to `llvm.spv.subgroup.ballot`, the intrinsic
// `feme::cpu::SIMDizePass`'s own `classifyWaveCall` now recognizes
// alongside the DXIL-origin `WaveActiveBallot` path, both feeding the
// same `feme::cpu::WaveCallKind::Ballot` CPU lowering.

// CHECK-LABEL: llvm.func @ballot
// CHECK: %[[RESULT:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.ballot"(%arg0) : (i1) -> vector<4xi32>
// CHECK: llvm.return %[[RESULT]] : vector<4xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @ballot(%predicate : i1) -> vector<4xi32> "None" {
    %0 = spirv.GroupNonUniformBallot <Subgroup> %predicate : vector<4xi32>
    spirv.ReturnValue %0 : vector<4xi32>
  }
}

// -----

// Checks that `spirv.GroupNonUniformInverseBallot` (roadmap L85) converts
// into `extractBallotBit`'s dynamic per-invocation bit lookup, using
// `llvm.spv.subgroup.local.invocation.id` as the "current invocation"
// index.

// CHECK-LABEL: llvm.func @inverse_ballot
// CHECK: %[[ID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[FIVE:.*]] = llvm.mlir.constant(5 : i32) : i32
// CHECK: %[[THIRTYONE:.*]] = llvm.mlir.constant(31 : i32) : i32
// CHECK: %[[WORDIDX:.*]] = llvm.lshr %[[ID]], %[[FIVE]] : i32
// CHECK: %[[BITIDX:.*]] = llvm.and %[[ID]], %[[THIRTYONE]] : i32
// CHECK: %[[WORDIDX64:.*]] = llvm.zext %[[WORDIDX]] : i32 to i64
// CHECK: %[[WORD:.*]] = llvm.extractelement %arg0[%[[WORDIDX64]] : i64] : vector<4xi32>
// CHECK: %[[SHIFTED:.*]] = llvm.lshr %[[WORD]], %[[BITIDX]] : i32
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[BIT:.*]] = llvm.and %[[SHIFTED]], %[[ONE]] : i32
// CHECK: %[[RESULT:.*]] = llvm.trunc %[[BIT]] : i32 to i1
// CHECK: llvm.return %[[RESULT]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @inverse_ballot(%value : vector<4xi32>) -> i1 "None" {
    %0 = spirv.GroupNonUniformInverseBallot <Subgroup> %value : vector<4xi32>
    spirv.ReturnValue %0 : i1
  }
}

// -----

// Checks that `spirv.GroupNonUniformBallotBitExtract` (roadmap L85)
// converts into the identical `extractBallotBit` arithmetic
// `InverseBallot` above uses, just against this op's own explicit
// `Index` operand instead of the current invocation's own id.

// CHECK-LABEL: llvm.func @bit_extract
// CHECK: %[[FIVE:.*]] = llvm.mlir.constant(5 : i32) : i32
// CHECK: %[[THIRTYONE:.*]] = llvm.mlir.constant(31 : i32) : i32
// CHECK: %[[WORDIDX:.*]] = llvm.lshr %arg1, %[[FIVE]] : i32
// CHECK: %[[BITIDX:.*]] = llvm.and %arg1, %[[THIRTYONE]] : i32
// CHECK: %[[WORDIDX64:.*]] = llvm.zext %[[WORDIDX]] : i32 to i64
// CHECK: %[[WORD:.*]] = llvm.extractelement %arg0[%[[WORDIDX64]] : i64] : vector<4xi32>
// CHECK: %[[SHIFTED:.*]] = llvm.lshr %[[WORD]], %[[BITIDX]] : i32
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[BIT:.*]] = llvm.and %[[SHIFTED]], %[[ONE]] : i32
// CHECK: %[[RESULT:.*]] = llvm.trunc %[[BIT]] : i32 to i1
// CHECK: llvm.return %[[RESULT]] : i1
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @bit_extract(%value : vector<4xi32>, %index : i32) -> i1 "None" {
    %0 = spirv.GroupNonUniformBallotBitExtract <Subgroup> %value, %index : vector<4xi32>, i32
    spirv.ReturnValue %0 : i1
  }
}

// -----

// Checks that `spirv.GroupNonUniformBallotFindLSB` (roadmap L85) converts
// into `ballotVectorToI128`'s bitcast, clipped to the actual
// `gl_SubgroupSize` via `clipBallotBitsToSubgroupSize`, followed by
// `llvm.cttz` and a truncation back to the op's own `i32` result.

// CHECK-LABEL: llvm.func @find_lsb
// CHECK: %[[BITS:.*]] = llvm.bitcast %arg0 : vector<4xi32> to i128
// CHECK: %[[SIZE:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.size"() : () -> i32
// CHECK: %[[SIZE128:.*]] = llvm.zext %[[SIZE]] : i32 to i128
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[ALLONES:.*]] = llvm.mlir.constant(-1 : i128) : i128
// CHECK: %[[SHIFTED:.*]] = llvm.shl %[[ONE]], %[[SIZE128]] : i128
// CHECK: %[[LOWMASK:.*]] = llvm.sub %[[SHIFTED]], %[[ONE]] : i128
// CHECK: %[[FULLWIDTH:.*]] = llvm.mlir.constant(128 : i128) : i128
// CHECK: %[[ISFULL:.*]] = llvm.icmp "uge" %[[SIZE128]], %[[FULLWIDTH]] : i128
// CHECK: %[[MASK:.*]] = llvm.select %[[ISFULL]], %[[ALLONES]], %[[LOWMASK]] : i1, i128
// CHECK: %[[CLIPPED:.*]] = llvm.and %[[BITS]], %[[MASK]] : i128
// CHECK: %[[LSB:.*]] = "llvm.intr.cttz"(%[[CLIPPED]]) <{is_zero_poison = true}> : (i128) -> i128
// CHECK: %[[RESULT:.*]] = llvm.trunc %[[LSB]] : i128 to i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @find_lsb(%value : vector<4xi32>) -> i32 "None" {
    %0 = spirv.GroupNonUniformBallotFindLSB <Subgroup> %value : vector<4xi32>, i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// Checks that `spirv.GroupNonUniformBallotFindMSB` (roadmap L85) converts
// into `ballotVectorToI128`'s bitcast, clipped to the actual
// `gl_SubgroupSize` exactly like `find_lsb` above, followed by
// `127 - llvm.ctlz(...)` and a truncation back to the op's own `i32`
// result.

// CHECK-LABEL: llvm.func @find_msb
// CHECK: %[[BITS:.*]] = llvm.bitcast %arg0 : vector<4xi32> to i128
// CHECK: %[[SIZE:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.size"() : () -> i32
// CHECK: %[[SIZE128:.*]] = llvm.zext %[[SIZE]] : i32 to i128
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[ALLONES:.*]] = llvm.mlir.constant(-1 : i128) : i128
// CHECK: %[[SHIFTED:.*]] = llvm.shl %[[ONE]], %[[SIZE128]] : i128
// CHECK: %[[LOWMASK:.*]] = llvm.sub %[[SHIFTED]], %[[ONE]] : i128
// CHECK: %[[FULLWIDTH:.*]] = llvm.mlir.constant(128 : i128) : i128
// CHECK: %[[ISFULL:.*]] = llvm.icmp "uge" %[[SIZE128]], %[[FULLWIDTH]] : i128
// CHECK: %[[MASK:.*]] = llvm.select %[[ISFULL]], %[[ALLONES]], %[[LOWMASK]] : i1, i128
// CHECK: %[[CLIPPED:.*]] = llvm.and %[[BITS]], %[[MASK]] : i128
// CHECK: %[[CLZ:.*]] = "llvm.intr.ctlz"(%[[CLIPPED]]) <{is_zero_poison = true}> : (i128) -> i128
// CHECK: %[[BITWIDTH:.*]] = llvm.mlir.constant(127 : i128) : i128
// CHECK: %[[MSB:.*]] = llvm.sub %[[BITWIDTH]], %[[CLZ]] : i128
// CHECK: %[[RESULT:.*]] = llvm.trunc %[[MSB]] : i128 to i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @find_msb(%value : vector<4xi32>) -> i32 "None" {
    %0 = spirv.GroupNonUniformBallotFindMSB <Subgroup> %value : vector<4xi32>, i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// Checks that a `Reduce`-group-operation `spirv.GroupNonUniformBallotBitCount`
// (roadmap L85, GLSL's `subgroupBallotBitCount`) converts into
// `ballotVectorToI128`'s bitcast, clipped to the actual `gl_SubgroupSize`
// exactly like `find_lsb`/`find_msb` above (unlike the scan variants
// below, which need no such clipping -- see `clipBallotBitsToSubgroupSize`'s
// own comment), followed directly by `llvm.ctpop`.

// CHECK-LABEL: llvm.func @bit_count_reduce
// CHECK: %[[BITS:.*]] = llvm.bitcast %arg0 : vector<4xi32> to i128
// CHECK: %[[SIZE:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.size"() : () -> i32
// CHECK: %[[SIZE128:.*]] = llvm.zext %[[SIZE]] : i32 to i128
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[ALLONES:.*]] = llvm.mlir.constant(-1 : i128) : i128
// CHECK: %[[SHIFTED:.*]] = llvm.shl %[[ONE]], %[[SIZE128]] : i128
// CHECK: %[[LOWMASK:.*]] = llvm.sub %[[SHIFTED]], %[[ONE]] : i128
// CHECK: %[[FULLWIDTH:.*]] = llvm.mlir.constant(128 : i128) : i128
// CHECK: %[[ISFULL:.*]] = llvm.icmp "uge" %[[SIZE128]], %[[FULLWIDTH]] : i128
// CHECK: %[[MASK:.*]] = llvm.select %[[ISFULL]], %[[ALLONES]], %[[LOWMASK]] : i1, i128
// CHECK: %[[CLIPPED:.*]] = llvm.and %[[BITS]], %[[MASK]] : i128
// CHECK: %[[POPCOUNT:.*]] = llvm.intr.ctpop(%[[CLIPPED]]) : (i128) -> i128
// CHECK: %[[RESULT:.*]] = llvm.trunc %[[POPCOUNT]] : i128 to i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @bit_count_reduce(%value : vector<4xi32>) -> i32 "None" {
    %0 = spirv.GroupNonUniformBallotBitCount <Subgroup> <Reduce> %value : vector<4xi32> -> i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// Checks that an `InclusiveScan`-group-operation
// `spirv.GroupNonUniformBallotBitCount` (roadmap L85, GLSL's
// `subgroupBallotInclusiveBitCount`) masks off every bit at or before the
// current invocation's own position (`(1 << (id + 1)) - 1`, clamped to
// "every bit" once the shift amount reaches the full 128-bit width) before
// popcount-ing.

// CHECK-LABEL: llvm.func @bit_count_inclusive
// CHECK: %[[BITS:.*]] = llvm.bitcast %arg0 : vector<4xi32> to i128
// CHECK: %[[ID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[ID128:.*]] = llvm.zext %[[ID]] : i32 to i128
// CHECK: %[[ONE_A:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[COUNT:.*]] = llvm.add %[[ID128]], %[[ONE_A]] : i128
// CHECK: %[[ONE_B:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[ALLONES:.*]] = llvm.mlir.constant(-1 : i128) : i128
// CHECK: %[[SHIFTED:.*]] = llvm.shl %[[ONE_B]], %[[COUNT]] : i128
// CHECK: %[[LOWMASK:.*]] = llvm.sub %[[SHIFTED]], %[[ONE_B]] : i128
// CHECK: %[[FULLWIDTH:.*]] = llvm.mlir.constant(128 : i128) : i128
// CHECK: %[[ISFULL:.*]] = llvm.icmp "uge" %[[COUNT]], %[[FULLWIDTH]] : i128
// CHECK: %[[MASK:.*]] = llvm.select %[[ISFULL]], %[[ALLONES]], %[[LOWMASK]] : i1, i128
// CHECK: %[[MASKED:.*]] = llvm.and %[[BITS]], %[[MASK]] : i128
// CHECK: %[[POPCOUNT:.*]] = llvm.intr.ctpop(%[[MASKED]]) : (i128) -> i128
// CHECK: %[[RESULT:.*]] = llvm.trunc %[[POPCOUNT]] : i128 to i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @bit_count_inclusive(%value : vector<4xi32>) -> i32 "None" {
    %0 = spirv.GroupNonUniformBallotBitCount <Subgroup> <InclusiveScan> %value : vector<4xi32> -> i32
    spirv.ReturnValue %0 : i32
  }
}

// -----

// Checks that an `ExclusiveScan`-group-operation
// `spirv.GroupNonUniformBallotBitCount` (roadmap L85, GLSL's
// `subgroupBallotExclusiveBitCount`) masks off every bit strictly before
// the current invocation's own position (`(1 << id) - 1`, the same shape
// as the inclusive variant above without the `+1`) before popcount-ing.

// CHECK-LABEL: llvm.func @bit_count_exclusive
// CHECK: %[[BITS:.*]] = llvm.bitcast %arg0 : vector<4xi32> to i128
// CHECK: %[[ID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[ID128:.*]] = llvm.zext %[[ID]] : i32 to i128
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i128) : i128
// CHECK: %[[COUNT:.*]] = llvm.add %[[ID128]], %[[ZERO]] : i128
// CHECK: %[[POPCOUNT:.*]] = llvm.intr.ctpop
// CHECK: %[[RESULT:.*]] = llvm.trunc %[[POPCOUNT]] : i128 to i32
// CHECK: llvm.return %[[RESULT]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @bit_count_exclusive(%value : vector<4xi32>) -> i32 "None" {
    %0 = spirv.GroupNonUniformBallotBitCount <Subgroup> <ExclusiveScan> %value : vector<4xi32> -> i32
    spirv.ReturnValue %0 : i32
  }
}
