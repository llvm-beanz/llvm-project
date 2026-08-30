// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that non-builtin `Input`/`Output` variables (ordinary stage-IO
// variables, e.g. a fragment shader's varyings) convert to an ordinary
// `llvm.mlir.global` in the address space LLVM's SPIRV backend expects that
// storage class to use (7/8, see `storageClassToAddressSpace` in
// `llvm/lib/Target/SPIRV/SPIRVUtils.h`) instead of failing to legalize --
// see the "Known gap" note this closes in feme/docs/Design.md's SPIR-V
// section (roadmap R19). `Location`/`Component`/`Index` and the boolean
// interpolation/per-primitive/per-patch decorations are preserved as a
// `feme.spirv.decorations` attribute (see
// feme::spirv::getStageIODecorationsAttrName), which
// feme::spirv::attachStageIODecorations later turns into real
// `!spirv.Decorations` LLVM metadata once a genuine llvm::Module exists (see
// test/Translate/SPIRV/spirv-to-llvmir-stage-io.mlir). A builtin interface
// block's own per-member decorations (roadmap H2c) are preserved the same
// way, under a distinct `feme.spirv.member.decorations` attribute -- see the
// last test below.

// CHECK: llvm.mlir.global external constant @in_var() {addr_space = 7 : i32, feme.spirv.decorations = {{\[}}[30 : i32, 0 : i32], [14 : i32]{{\]}}} : i32
// CHECK-LABEL: llvm.func @read_stage_io
// CHECK: %[[PTR:.*]] = llvm.mlir.addressof @in_var : !llvm.ptr<7>
// CHECK: llvm.load %[[PTR]] : !llvm.ptr<7> -> i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @in_var {location = 0 : i32, flat} : !spirv.ptr<i32, Input>
  spirv.func @read_stage_io() -> i32 "None" {
    %0 = spirv.mlir.addressof @in_var : !spirv.ptr<i32, Input>
    %1 = spirv.Load "Input" %0 : i32
    spirv.ReturnValue %1 : i32
  }
}

// -----

// An `Output` variable is ordinary memory too, just in address space 8
// rather than 7, and is written rather than read.

// CHECK: llvm.mlir.global external @out_var() {addr_space = 8 : i32, feme.spirv.decorations = {{\[}}[30 : i32, 1 : i32]{{\]}}} : f32
// CHECK-LABEL: llvm.func @write_stage_io
// CHECK: %[[PTR:.*]] = llvm.mlir.addressof @out_var : !llvm.ptr<8>
// CHECK: llvm.store %{{.*}}, %[[PTR]] : f32, !llvm.ptr<8>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @out_var {location = 1 : i32} : !spirv.ptr<f32, Output>
  spirv.func @write_stage_io(%v: f32) -> () "None" {
    %0 = spirv.mlir.addressof @out_var : !spirv.ptr<f32, Output>
    spirv.Store "Output" %0, %v : f32
    spirv.Return
  }
}

// -----

// `Component`/`Index` and every other boolean decoration this converts
// (`no_perspective`/`centroid`/`sample`/`patch`/`per_primitive_ext`) all
// fold into the same `feme.spirv.decorations` attribute alongside
// `Location`.

// CHECK: feme.spirv.decorations = {{\[}}[30 : i32, 2 : i32], [31 : i32, 1 : i32], [32 : i32, 3 : i32], [13 : i32], [15 : i32], [16 : i32], [17 : i32], [5271 : i32]{{\]}}
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @in_var {location = 2 : i32, component = 1 : i32,
                                index = 3 : i32, no_perspective, centroid,
                                sample, patch, per_primitive_ext}
      : !spirv.ptr<f32, Input>
}

// -----

// A builtin `Input` variable still converts through BuiltInAddressOfPattern
// (the `llvm.spv.*` intrinsic), never through the ordinary-memory stage-IO
// path above, since the two are mutually exclusive on the same variable.

// CHECK-NOT: llvm.mlir.global
// CHECK-LABEL: llvm.func @read_builtin
// CHECK: llvm.call_intrinsic "llvm.spv.flattened.thread.id.in.group"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @idx built_in("LocalInvocationIndex") : !spirv.ptr<i32, Input>
  spirv.func @read_builtin() -> i32 "None" {
    %0 = spirv.mlir.addressof @idx : !spirv.ptr<i32, Input>
    %1 = spirv.Load "Input" %0 : i32
    spirv.ReturnValue %1 : i32
  }
}

// -----

// A *graphics* builtin (`Position` here) has no `llvm.spv.*` intrinsic to
// legalize to, so it converts through the ordinary stage-IO path above --
// with its `BuiltIn` decoration (code 11) preserved, which is what lets
// `feme::graphics::CanonicalizeStagePass` recover the element's
// `feme::SignatureSystemValue` identity later.

// CHECK: llvm.mlir.global external @gl_Position() {addr_space = 8 : i32, feme.spirv.decorations = {{\[}}[11 : i32, 0 : i32]{{\]}}} : vector<4xf32>
// CHECK-LABEL: llvm.func @write_position
// CHECK: %[[PTR:.*]] = llvm.mlir.addressof @gl_Position : !llvm.ptr<8>
// CHECK: llvm.store %{{.*}}, %[[PTR]] : vector<4xf32>, !llvm.ptr<8>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gl_Position built_in("Position") : !spirv.ptr<vector<4xf32>, Output>
  spirv.func @write_position(%v: vector<4xf32>) -> () "None" {
    %0 = spirv.mlir.addressof @gl_Position : !spirv.ptr<vector<4xf32>, Output>
    spirv.Store "Output" %0, %v : vector<4xf32>
    spirv.Return
  }
}

// -----

// (Roadmap H2c) A builtin *interface block* (glslang's implicit
// `gl_PerVertex`, `{gl_Position, gl_PointSize, gl_ClipDistance,
// gl_CullDistance}`) has no whole-variable `BuiltIn` attribute at all --
// SPIR-V decorates its members individually (`OpMemberDecorate`) -- but
// still converts through this same ordinary stage-IO path (its storage
// class is `Output`, and getBuiltInMapping only ever matches a
// whole-variable `built_in` attribute, which this op does not have). Its
// members' own `BuiltIn` decorations are preserved as a
// `feme.spirv.member.decorations` attribute instead of being silently
// dropped, which is what lets `feme::graphics::CanonicalizeStagePass`
// (roadmap H2d) later recover each member's own system-value identity.

// CHECK: llvm.mlir.global external @gl_PerVertex() {addr_space = 8 : i32, feme.spirv.member.decorations = {{\[}}[0 : i32, {{\[}}[11 : i32, 0 : i32]{{\]}}], [1 : i32, {{\[}}[11 : i32, 1 : i32]{{\]}}], [2 : i32, {{\[}}[11 : i32, 3 : i32]{{\]}}], [3 : i32, {{\[}}[11 : i32, 4 : i32]{{\]}}]{{\]}}}
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gl_PerVertex : !spirv.ptr<!spirv.struct<(vector<4xf32> [BuiltIn=0 : i32], f32 [BuiltIn=1 : i32], !spirv.array<1 x f32> [BuiltIn=3 : i32], !spirv.array<1 x f32> [BuiltIn=4 : i32])>, Output>
}

// -----

// (Roadmap H5g) A geometry entry's own per-vertex builtin interface block
// (`gl_in[]`) wraps the same per-member-decorated block struct as
// `gl_PerVertex` above in one more array dimension -- `Input` storage
// class here, since a geometry entry reads it rather than writing it --
// but the member decorations that matter still live on the inner struct,
// so they are recovered the same way, keyed off that inner struct
// regardless of the outer array. `CanonicalizeStage.cpp`'s own
// `addElements` (roadmap H5b) peels the outer array dimension back off
// once this metadata is present to peel in front of.

// CHECK: llvm.mlir.global external constant @gl_in() {addr_space = 7 : i32, feme.spirv.member.decorations = {{\[}}[0 : i32, {{\[}}[11 : i32, 0 : i32]{{\]}}], [1 : i32, {{\[}}[11 : i32, 1 : i32]{{\]}}]{{\]}}}
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, Geometry], []> {
  spirv.GlobalVariable @gl_in : !spirv.ptr<!spirv.array<3 x !spirv.struct<(vector<4xf32> [BuiltIn=0 : i32], f32 [BuiltIn=1 : i32])>>, Input>
}

// -----

// (Roadmap H7x) A fragment stage's own read of `gl_ClipDistance`/
// `gl_CullDistance` -- unlike the vertex-stage `Output` side, glslang
// emits these as a standalone (not `gl_PerVertex`-block-wrapped) `Input`
// array global, `BuiltIn`-decorated same as any other builtin, so it
// converts through this ordinary stage-IO path (StageIOAddressOfPattern),
// which -- like any non-builtin `Input` -- eagerly loads the whole array
// at the `spirv.mlir.addressof` site, producing a value rather than a
// pointer. A single *constant*-indexed `spirv.AccessChain` into that
// value converts directly to `llvm.extractvalue` rather than falling
// through to MLIR's own pointer-assuming `spirv.AccessChain` pattern.

// CHECK-LABEL: llvm.func @read_clip_distance_0
// CHECK: %[[ARR:.*]] = llvm.load %{{.*}} : !llvm.ptr<7> -> !llvm.array<1 x f32>
// CHECK: llvm.extractvalue %[[ARR]][0] : !llvm.array<1 x f32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ClipDistance], []> {
  spirv.GlobalVariable @gl_ClipDistance built_in("ClipDistance") : !spirv.ptr<!spirv.array<1 x f32>, Input>
  spirv.func @read_clip_distance_0() -> f32 "None" {
    %0 = spirv.mlir.addressof @gl_ClipDistance : !spirv.ptr<!spirv.array<1 x f32>, Input>
    %c0 = spirv.Constant 0 : i32
    %1 = spirv.AccessChain %0[%c0] : !spirv.ptr<!spirv.array<1 x f32>, Input>, i32 -> !spirv.ptr<f32, Input>
    %2 = spirv.Load "Input" %1 : f32
    spirv.ReturnValue %2 : f32
  }
}
