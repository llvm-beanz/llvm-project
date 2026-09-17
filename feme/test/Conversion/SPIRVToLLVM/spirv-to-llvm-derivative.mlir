// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H124d: each plain (non-`fwidth`) derivative op converts directly
// into a call to its already-existing matching `llvm.spv.ddx`/`.ddy`/
// `.ddx.fine`/`.ddy.fine`/`.ddx.coarse`/`.ddy.coarse` intrinsic, which
// `feme::graphics::CanonicalizeStagePass` already fully consumes -- see
// `DerivativeConversionPattern` (SPIRVToLLVMPatterns.cpp).

// CHECK-LABEL: llvm.func @dpdx
// CHECK: llvm.call_intrinsic "llvm.spv.ddx"(%{{.*}}) : (f32) -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @dpdx(%arg: f32) -> f32 "None" {
    %0 = spirv.DPdx %arg : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// CHECK-LABEL: llvm.func @dpdy
// CHECK: llvm.call_intrinsic "llvm.spv.ddy"(%{{.*}}) : (f32) -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @dpdy(%arg: f32) -> f32 "None" {
    %0 = spirv.DPdy %arg : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// CHECK-LABEL: llvm.func @dpdx_fine
// CHECK: llvm.call_intrinsic "llvm.spv.ddx.fine"(%{{.*}}) : (f32) -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DerivativeControl], []> {
  spirv.func @dpdx_fine(%arg: f32) -> f32 "None" {
    %0 = spirv.DPdxFine %arg : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// CHECK-LABEL: llvm.func @dpdy_fine
// CHECK: llvm.call_intrinsic "llvm.spv.ddy.fine"(%{{.*}}) : (f32) -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DerivativeControl], []> {
  spirv.func @dpdy_fine(%arg: f32) -> f32 "None" {
    %0 = spirv.DPdyFine %arg : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// CHECK-LABEL: llvm.func @dpdx_coarse
// CHECK: llvm.call_intrinsic "llvm.spv.ddx.coarse"(%{{.*}}) : (f32) -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DerivativeControl], []> {
  spirv.func @dpdx_coarse(%arg: f32) -> f32 "None" {
    %0 = spirv.DPdxCoarse %arg : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// CHECK-LABEL: llvm.func @dpdy_coarse
// CHECK: llvm.call_intrinsic "llvm.spv.ddy.coarse"(%{{.*}}) : (f32) -> f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DerivativeControl], []> {
  spirv.func @dpdy_coarse(%arg: f32) -> f32 "None" {
    %0 = spirv.DPdyCoarse %arg : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// `OpFwidth` (implicit precision) expands to `fabs(ddx.fine) + fabs(ddy.fine)`
// -- the fine pair, matching `CanonicalizeStage.cpp`'s own existing
// convention of never coarsening precision the source did not explicitly
// ask for. See `FwidthConversionPattern`.

// CHECK-LABEL: llvm.func @fwidth
// CHECK: %[[DDX:.*]] = llvm.call_intrinsic "llvm.spv.ddx.fine"(%{{.*}}) : (f32) -> f32
// CHECK: %[[DDY:.*]] = llvm.call_intrinsic "llvm.spv.ddy.fine"(%{{.*}}) : (f32) -> f32
// CHECK: %[[ABSX:.*]] = llvm.intr.fabs(%[[DDX]])
// CHECK: %[[ABSY:.*]] = llvm.intr.fabs(%[[DDY]])
// CHECK: llvm.fadd %[[ABSX]], %[[ABSY]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @fwidth(%arg: f32) -> f32 "None" {
    %0 = spirv.Fwidth %arg : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// CHECK-LABEL: llvm.func @fwidth_fine
// CHECK: llvm.call_intrinsic "llvm.spv.ddx.fine"
// CHECK: llvm.call_intrinsic "llvm.spv.ddy.fine"
// CHECK: llvm.intr.fabs
// CHECK: llvm.intr.fabs
// CHECK: llvm.fadd
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DerivativeControl], []> {
  spirv.func @fwidth_fine(%arg: f32) -> f32 "None" {
    %0 = spirv.FwidthFine %arg : f32
    spirv.ReturnValue %0 : f32
  }
}

// -----

// CHECK-LABEL: llvm.func @fwidth_coarse
// CHECK: llvm.call_intrinsic "llvm.spv.ddx.coarse"
// CHECK: llvm.call_intrinsic "llvm.spv.ddy.coarse"
// CHECK: llvm.intr.fabs
// CHECK: llvm.intr.fabs
// CHECK: llvm.fadd
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, DerivativeControl], []> {
  spirv.func @fwidth_coarse(%arg: f32) -> f32 "None" {
    %0 = spirv.FwidthCoarse %arg : f32
    spirv.ReturnValue %0 : f32
  }
}
