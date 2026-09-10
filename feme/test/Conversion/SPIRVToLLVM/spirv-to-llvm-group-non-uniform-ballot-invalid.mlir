// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// `BallotConversionPattern` (roadmap L85) only implements `Subgroup`
// execution scope, mirroring `ElectConversionPattern`/
// `ShuffleConversionPattern`: no known dxc/glslang-compiled shape needs
// `Workgroup`-scope ballot.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @workgroup_ballot(%predicate : i1) -> vector<4xi32> "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformBallot' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformBallot <Workgroup> %predicate : vector<4xi32>
    spirv.ReturnValue %0 : vector<4xi32>
  }
}

// -----

// `BallotBitExtractConversionPattern` (roadmap L85) only implements a
// 32-bit `Index` operand, mirroring `ShuffleXorConversionPattern`'s own
// "mask must be 32-bit" restriction: every known dxc/glslang-compiled
// `subgroupBallotBitExtract` call site's own `uint` index operand is
// already 32-bit.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @bit_extract_wide_index(%value : vector<4xi32>, %index : i64) -> i1 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformBallotBitExtract' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformBallotBitExtract <Subgroup> %value, %index : vector<4xi32>, i64
    spirv.ReturnValue %0 : i1
  }
}

// -----

// `BallotFindLSBConversionPattern` (roadmap L85) only implements a
// 32-bit result, mirroring `BallotBitExtractConversionPattern` above:
// every known dxc/glslang-compiled `subgroupBallotFindLSB` call site's
// own `uint` result is already 32-bit.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @find_lsb_wide_result(%value : vector<4xi32>) -> i64 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformBallotFindLSB' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformBallotFindLSB <Subgroup> %value : vector<4xi32>, i64
    spirv.ReturnValue %0 : i64
  }
}

// -----

// `BallotFindMSBConversionPattern` (roadmap L85) only implements a
// 32-bit result, mirroring `BallotFindLSBConversionPattern` above.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @find_msb_wide_result(%value : vector<4xi32>) -> i64 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformBallotFindMSB' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformBallotFindMSB <Subgroup> %value : vector<4xi32>, i64
    spirv.ReturnValue %0 : i64
  }
}

// -----

// `BallotBitCountConversionPattern` (roadmap L85) only implements a
// 32-bit result, mirroring `BallotFindLSBConversionPattern` above.

spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.func @bit_count_wide_result(%value : vector<4xi32>) -> i64 "None" {
    // expected-error@+1 {{failed to legalize operation 'spirv.GroupNonUniformBallotBitCount' that was explicitly marked illegal}}
    %0 = spirv.GroupNonUniformBallotBitCount <Subgroup> <Reduce> %value : vector<4xi32> -> i64
    spirv.ReturnValue %0 : i64
  }
}
