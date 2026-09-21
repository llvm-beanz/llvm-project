// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L115(b) regression test: `spirv.GL.InterpolateAt{Centroid,Sample,
// Offset}` previously failed pipeline creation entirely ("failed to
// legalize operation ... that was explicitly marked illegal", no
// conversion pattern registered at all). Once patterns were added, a
// second, narrower bug surfaced: the naive approach of special-casing
// `StageIOAddressOfPattern` to keep a real pointer for an `Interpolant`
// operand's own `spirv.mlir.addressof` was silently undone by MLIR's own
// dialect-conversion materialization (any `Adaptor`-based consumer of that
// addressof still observed the type converter's *canonical* answer -- an
// eagerly-loaded value, not a pointer). `resolveInterpolantAddress()`
// fixes this by reaching back through that eager load's own address
// operand instead of fighting the materialization. These three cases
// exercise all three ops together, confirming each still resolves its own
// real `Input`-global address rather than failing to legalize or
// (silently) operating on the wrong value.

// CHECK-LABEL: llvm.func @interpolate_at_centroid
// CHECK: %[[ADDR:.*]] = llvm.mlir.addressof @vColor
// CHECK: %{{.*}} = llvm.call @feme.spirv.interpolate_at_centroid.v4f32(%[[ADDR]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.func @interpolate_at_centroid() -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @vColor : !spirv.ptr<vector<4xf32>, Input>
    %1 = spirv.GL.InterpolateAtCentroid %0 : !spirv.ptr<vector<4xf32>, Input> -> vector<4xf32>
    spirv.ReturnValue %1 : vector<4xf32>
  }
  spirv.EntryPoint "Fragment" @interpolate_at_centroid
  spirv.ExecutionMode @interpolate_at_centroid "OriginUpperLeft"
}

// -----

// CHECK-LABEL: llvm.func @interpolate_at_sample
// CHECK: %[[ADDR:.*]] = llvm.mlir.addressof @vColor
// CHECK: %{{.*}} = llvm.call @feme.spirv.interpolate_at_sample.v4f32(%[[ADDR]], %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, SampleRateShading], []> {
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.func @interpolate_at_sample(%sample: i32) -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @vColor : !spirv.ptr<vector<4xf32>, Input>
    %1 = spirv.GL.InterpolateAtSample %0, %sample : !spirv.ptr<vector<4xf32>, Input>, i32 -> vector<4xf32>
    spirv.ReturnValue %1 : vector<4xf32>
  }
  spirv.EntryPoint "Fragment" @interpolate_at_sample
  spirv.ExecutionMode @interpolate_at_sample "OriginUpperLeft"
}

// -----

// CHECK-LABEL: llvm.func @interpolate_at_offset
// CHECK: %[[ADDR:.*]] = llvm.mlir.addressof @vColor
// CHECK: %{{.*}} = llvm.call @feme.spirv.interpolate_at_offset.v4f32(%[[ADDR]], %{{.*}}, %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, InterpolationFunction], []> {
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.func @interpolate_at_offset() -> vector<4xf32> "None" {
    %0 = spirv.mlir.addressof @vColor : !spirv.ptr<vector<4xf32>, Input>
    %off = spirv.Constant dense<[0.1, 0.2]> : vector<2xf32>
    %1 = spirv.GL.InterpolateAtOffset %0, %off : !spirv.ptr<vector<4xf32>, Input>, vector<2xf32> -> vector<4xf32>
    spirv.ReturnValue %1 : vector<4xf32>
  }
  spirv.EntryPoint "Fragment" @interpolate_at_offset
  spirv.ExecutionMode @interpolate_at_offset "OriginUpperLeft"
}
