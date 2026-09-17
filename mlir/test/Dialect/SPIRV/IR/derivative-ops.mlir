// RUN: mlir-opt --split-input-file --verify-diagnostics %s | FileCheck %s

//===----------------------------------------------------------------------===//
// spirv.DPdx
//===----------------------------------------------------------------------===//

func.func @dpdx(%arg: f32) -> f32 {
  // CHECK: spirv.DPdx {{%.*}} : f32
  %0 = spirv.DPdx %arg : f32
  spirv.ReturnValue %0 : f32
}

// -----

//===----------------------------------------------------------------------===//
// spirv.DPdy
//===----------------------------------------------------------------------===//

func.func @dpdy(%arg: vector<3xf32>) -> vector<3xf32> {
  // CHECK: spirv.DPdy {{%.*}} : vector<3xf32>
  %0 = spirv.DPdy %arg : vector<3xf32>
  spirv.ReturnValue %0 : vector<3xf32>
}

// -----

//===----------------------------------------------------------------------===//
// spirv.Fwidth
//===----------------------------------------------------------------------===//

func.func @fwidth(%arg: f32) -> f32 {
  // CHECK: spirv.Fwidth {{%.*}} : f32
  %0 = spirv.Fwidth %arg : f32
  spirv.ReturnValue %0 : f32
}

// -----

//===----------------------------------------------------------------------===//
// spirv.DPdxFine / spirv.DPdyFine / spirv.FwidthFine
//===----------------------------------------------------------------------===//

func.func @derivative_fine(%arg: f32) -> f32 {
  // CHECK: spirv.DPdxFine {{%.*}} : f32
  %0 = spirv.DPdxFine %arg : f32
  // CHECK: spirv.DPdyFine {{%.*}} : f32
  %1 = spirv.DPdyFine %arg : f32
  // CHECK: spirv.FwidthFine {{%.*}} : f32
  %2 = spirv.FwidthFine %arg : f32
  spirv.ReturnValue %2 : f32
}

// -----

//===----------------------------------------------------------------------===//
// spirv.DPdxCoarse / spirv.DPdyCoarse / spirv.FwidthCoarse
//===----------------------------------------------------------------------===//

func.func @derivative_coarse(%arg: f32) -> f32 {
  // CHECK: spirv.DPdxCoarse {{%.*}} : f32
  %0 = spirv.DPdxCoarse %arg : f32
  // CHECK: spirv.DPdyCoarse {{%.*}} : f32
  %1 = spirv.DPdyCoarse %arg : f32
  // CHECK: spirv.FwidthCoarse {{%.*}} : f32
  %2 = spirv.FwidthCoarse %arg : f32
  spirv.ReturnValue %2 : f32
}

// -----

func.func @dpdx_mismatched_type(%arg: f32) -> f32 {
  // expected-error @+1 {{failed to verify that all of {p, result} have same type}}
  %0 = "spirv.DPdx"(%arg) : (f32) -> vector<2xf32>
  spirv.ReturnValue %arg : f32
}
