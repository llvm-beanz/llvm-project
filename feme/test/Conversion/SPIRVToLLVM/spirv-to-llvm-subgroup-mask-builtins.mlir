// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that the five `GroupNonUniformBallot`-gated subgroup mask builtin
// *variables* (roadmap L89g) convert to computed `i128` masks over
// `llvm.spv.subgroup.local.invocation.id`, bitcast to the `uvec4` the SPIR-V
// ballot ABI spells them as.
//
// Unlike every `BuiltInMappings[]` entry, none of these is a single
// `llvm.spv.*` intrinsic read, so each needs its own expansion in
// `buildSubgroupMask`. Before that existed they fell through to the generic
// `Input`-variable path and became a `feme.stage.input.load` -- a graphics
// stage op no compute-stage lowering handles, which reached the JIT as an
// unresolved `feme.stage.input.load.v4i32` symbol.

// CHECK-LABEL: llvm.func @eq_mask
// CHECK-NOT: llvm.mlir.addressof
// CHECK: %[[ID:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.local.invocation.id"() : () -> i32
// CHECK: %[[ID128:.*]] = llvm.zext %[[ID]] : i32 to i128
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[EQ:.*]] = llvm.shl %[[ONE]], %[[ID128]] : i128
// CHECK: %[[RES:.*]] = llvm.bitcast %[[EQ]] : i128 to vector<4xi32>
// CHECK: llvm.return %[[RES]] : vector<4xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.GlobalVariable @eq built_in("SubgroupEqMask") : !spirv.ptr<vector<4xi32>, Input>
  spirv.func @eq_mask() -> vector<4xi32> "None" {
    %p = spirv.mlir.addressof @eq : !spirv.ptr<vector<4xi32>, Input>
    %v = spirv.Load "Input" %p : vector<4xi32>
    spirv.ReturnValue %v : vector<4xi32>
  }
}

// -----

// `LtMask` is the prefix strictly below the current invocation, `(1 << id) - 1`.
// It needs no clip to `SubgroupSize`: `id` is always less than the subgroup
// size, so the prefix is already bounded by it.

// CHECK-LABEL: llvm.func @lt_mask
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[EQ:.*]] = llvm.shl %[[ONE]], %{{.*}} : i128
// CHECK: %[[LT:.*]] = llvm.sub %[[EQ]], %[[ONE]] : i128
// CHECK: llvm.bitcast %[[LT]] : i128 to vector<4xi32>
// CHECK-NOT: llvm.call_intrinsic "llvm.spv.subgroup.size"
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.GlobalVariable @lt built_in("SubgroupLtMask") : !spirv.ptr<vector<4xi32>, Input>
  spirv.func @lt_mask() -> vector<4xi32> "None" {
    %p = spirv.mlir.addressof @lt : !spirv.ptr<vector<4xi32>, Input>
    %v = spirv.Load "Input" %p : vector<4xi32>
    spirv.ReturnValue %v : vector<4xi32>
  }
}

// -----

// `LeMask` is the prefix through the current invocation. It is built as
// `(EqMask << 1) - 1` rather than the equivalent-looking `1 << (id + 1)`,
// which would be an out-of-range shift at the largest valid `id` (127, in a
// full 128-wide subgroup).

// CHECK-LABEL: llvm.func @le_mask
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[EQ:.*]] = llvm.shl %[[ONE]], %{{.*}} : i128
// CHECK: %[[SHIFTED:.*]] = llvm.shl %[[EQ]], %[[ONE]] : i128
// CHECK: %[[LE:.*]] = llvm.sub %[[SHIFTED]], %[[ONE]] : i128
// CHECK: llvm.bitcast %[[LE]] : i128 to vector<4xi32>
// CHECK-NOT: llvm.call_intrinsic "llvm.spv.subgroup.size"
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.GlobalVariable @le built_in("SubgroupLeMask") : !spirv.ptr<vector<4xi32>, Input>
  spirv.func @le_mask() -> vector<4xi32> "None" {
    %p = spirv.mlir.addressof @le : !spirv.ptr<vector<4xi32>, Input>
    %v = spirv.Load "Input" %p : vector<4xi32>
    spirv.ReturnValue %v : vector<4xi32>
  }
}

// -----

// `GeMask` is the complement of `LtMask`, and unlike the prefix masks it
// *does* need clipping to `SubgroupSize`: complementing a prefix sets every
// bit above the subgroup, and the spec defines those as zero.

// CHECK-LABEL: llvm.func @ge_mask
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[EQ:.*]] = llvm.shl %[[ONE]], %{{.*}} : i128
// CHECK: %[[LT:.*]] = llvm.sub %[[EQ]], %[[ONE]] : i128
// CHECK: %[[ALLONES:.*]] = llvm.mlir.constant(-1 : i128) : i128
// CHECK: %[[GE:.*]] = llvm.xor %[[LT]], %[[ALLONES]] : i128
// CHECK: %[[SIZE:.*]] = llvm.call_intrinsic "llvm.spv.subgroup.size"() : () -> i32
// CHECK: %[[CLIPPED:.*]] = llvm.and %[[GE]], %{{.*}} : i128
// CHECK: llvm.bitcast %[[CLIPPED]] : i128 to vector<4xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.GlobalVariable @ge built_in("SubgroupGeMask") : !spirv.ptr<vector<4xi32>, Input>
  spirv.func @ge_mask() -> vector<4xi32> "None" {
    %p = spirv.mlir.addressof @ge : !spirv.ptr<vector<4xi32>, Input>
    %v = spirv.Load "Input" %p : vector<4xi32>
    spirv.ReturnValue %v : vector<4xi32>
  }
}

// -----

// `GtMask` is the complement of `LeMask`, clipped for the same reason.

// CHECK-LABEL: llvm.func @gt_mask
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1 : i128) : i128
// CHECK: %[[EQ:.*]] = llvm.shl %[[ONE]], %{{.*}} : i128
// CHECK: %[[SHIFTED:.*]] = llvm.shl %[[EQ]], %[[ONE]] : i128
// CHECK: %[[LE:.*]] = llvm.sub %[[SHIFTED]], %[[ONE]] : i128
// CHECK: %[[ALLONES:.*]] = llvm.mlir.constant(-1 : i128) : i128
// CHECK: %[[GT:.*]] = llvm.xor %[[LE]], %[[ALLONES]] : i128
// CHECK: llvm.call_intrinsic "llvm.spv.subgroup.size"() : () -> i32
// CHECK: %[[CLIPPED:.*]] = llvm.and %[[GT]], %{{.*}} : i128
// CHECK: llvm.bitcast %[[CLIPPED]] : i128 to vector<4xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.GlobalVariable @gt built_in("SubgroupGtMask") : !spirv.ptr<vector<4xi32>, Input>
  spirv.func @gt_mask() -> vector<4xi32> "None" {
    %p = spirv.mlir.addressof @gt : !spirv.ptr<vector<4xi32>, Input>
    %v = spirv.Load "Input" %p : vector<4xi32>
    spirv.ReturnValue %v : vector<4xi32>
  }
}

// -----

// A single-component access chain into a mask builtin (`gl_SubgroupEqMask.x`,
// the shape glslang emits when only one word is read) needs no new pattern:
// `BuiltInAccessChainPattern` already keys off the base converting to a
// vector *value* rather than a pointer, which is now true for these too.

// CHECK-LABEL: llvm.func @eq_mask_component
// CHECK-NOT: llvm.getelementptr
// CHECK: %[[MASK:.*]] = llvm.bitcast %{{.*}} : i128 to vector<4xi32>
// CHECK: %[[WORD:.*]] = llvm.extractelement %[[MASK]][%{{.*}} : i32] : vector<4xi32>
// CHECK: llvm.return %[[WORD]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.3, [Shader, GroupNonUniform, GroupNonUniformBallot], []> {
  spirv.GlobalVariable @eqc built_in("SubgroupEqMask") : !spirv.ptr<vector<4xi32>, Input>
  spirv.func @eq_mask_component() -> i32 "None" {
    %idx = spirv.Constant 0 : i32
    %p = spirv.mlir.addressof @eqc : !spirv.ptr<vector<4xi32>, Input>
    %c = spirv.AccessChain %p[%idx] : !spirv.ptr<vector<4xi32>, Input>, i32 -> !spirv.ptr<i32, Input>
    %v = spirv.Load "Input" %c : i32
    spirv.ReturnValue %v : i32
  }
}
