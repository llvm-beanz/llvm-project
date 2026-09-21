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

// -----

// Roadmap L125(r) regression test: an `Interpolant` reached through a
// `spirv.AccessChain` into an *array*-typed `Input` global (e.g. a
// per-vertex-arrayed or struct-member leaf), rather than a bare
// `spirv.mlir.addressof` of the whole variable. `StageIOArrayAccessChainPattern`
// converts this `AccessChain` to a real `llvm.getelementptr`, but the type
// converter's own canonical (eagerly-loaded) answer for that leaf means the
// dialect-conversion driver only materializes the eventual `llvm.load` lazily,
// via a deferred `unrealized_conversion_cast` this pattern otherwise never sees
// through. `resolveInterpolantAddress()` unwraps that cast directly instead of
// waiting for the deferred materialization, recovering the real GEP address.
// CHECK-LABEL: llvm.func @interpolate_at_centroid_array_access_chain
// CHECK: %[[GEP:.*]] = llvm.getelementptr
// CHECK: %{{.*}} = llvm.call @feme.spirv.interpolate_at_centroid.v2f32(%[[GEP]])
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<!spirv.array<2 x vector<2xf32>>, Input>
  spirv.func @interpolate_at_centroid_array_access_chain() -> vector<2xf32> "None" {
    %idx = spirv.Constant 0 : i32
    %0 = spirv.mlir.addressof @vColor : !spirv.ptr<!spirv.array<2 x vector<2xf32>>, Input>
    %1 = spirv.AccessChain %0[%idx] : !spirv.ptr<!spirv.array<2 x vector<2xf32>>, Input>, i32 -> !spirv.ptr<vector<2xf32>, Input>
    %2 = spirv.GL.InterpolateAtCentroid %1 : !spirv.ptr<vector<2xf32>, Input> -> vector<2xf32>
    spirv.ReturnValue %2 : vector<2xf32>
  }
  spirv.EntryPoint "Fragment" @interpolate_at_centroid_array_access_chain
  spirv.ExecutionMode @interpolate_at_centroid_array_access_chain "OriginUpperLeft"
}

// -----

// Roadmap L125(r) regression test: an `Interpolant` reached through a
// `spirv.AccessChain` selecting a single *component* of a bare vector-typed
// (not array/struct) `Input` global -- e.g. GLSL's own
// `interpolateAtSample(vColor.x, ...)`. Unlike the array-leaf case above, a
// whole vector `Input` variable is always eagerly loaded as one value, so
// this `AccessChain` converts not to a GEP but to an `llvm.extractelement`
// reading one lane out of the already-loaded vector: by the time this
// pattern runs, there is no pointer/address left anywhere in the IR at all.
// `resolveInterpolantAddress()` handles this by recovering the vector
// operand's own address and re-synthesizing a GEP into it using the
// extraction's own lane index, giving `feme.spirv.interpolate_at_sample`
// the real address it needs.
// CHECK-LABEL: llvm.func @interpolate_at_sample_vector_access_chain
// CHECK: %[[LOAD:.*]] = llvm.load %[[ADDR:.*]] : !llvm.ptr<7> -> vector<4xf32>
// CHECK: %[[GEP:.*]] = llvm.getelementptr %[[ADDR]]
// CHECK: %{{.*}} = llvm.call @feme.spirv.interpolate_at_sample.f32(%[[GEP]], %{{.*}})
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, SampleRateShading], []> {
  spirv.GlobalVariable @vColor {location = 0 : i32} : !spirv.ptr<vector<4xf32>, Input>
  spirv.func @interpolate_at_sample_vector_access_chain(%sampleid: i32) -> f32 "None" {
    %idx = spirv.Constant 0 : i32
    %0 = spirv.mlir.addressof @vColor : !spirv.ptr<vector<4xf32>, Input>
    %1 = spirv.AccessChain %0[%idx] : !spirv.ptr<vector<4xf32>, Input>, i32 -> !spirv.ptr<f32, Input>
    %2 = spirv.GL.InterpolateAtSample %1, %sampleid : !spirv.ptr<f32, Input>, i32 -> f32
    spirv.ReturnValue %2 : f32
  }
  spirv.EntryPoint "Fragment" @interpolate_at_sample_vector_access_chain
  spirv.ExecutionMode @interpolate_at_sample_vector_access_chain "OriginUpperLeft"
}
