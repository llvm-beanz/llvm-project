// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H124k: `spirv.GL.PackHalf2x16`/`spirv.GL.UnpackHalf2x16` (HLSL's
// `f32tof16`/`f16tof32`) had no conversion pattern at all before this fix
// -- neither this file's own `populateSPIRVToLLVMTargetPatterns` nor
// upstream MLIR's own `populateSPIRVToLLVMConversionPatterns` registered
// one for either, so both HLSL intrinsics failed pipeline creation with
// "failed to legalize operation ... that was explicitly marked illegal".

// `PackHalf2x16(x)` truncates each lane of `x` to `f16`, reinterprets it
// as `i16`, then packs lane 0 into the low 16 bits and lane 1 (shifted
// left by 16) into the high 16 bits of the `i32` result.
// CHECK-LABEL: llvm.func @pack_half2x16
// CHECK: %[[LANE0:.*]] = llvm.extractelement %arg0{{.*}} : vector<2xf32>
// CHECK: %[[HALF0:.*]] = llvm.fptrunc %[[LANE0]] : f32 to f16
// CHECK: %[[BITS0:.*]] = llvm.bitcast %[[HALF0]] : f16 to i16
// CHECK: %[[LOW:.*]] = llvm.zext %[[BITS0]] : i16 to i32
// CHECK: %[[LANE1:.*]] = llvm.extractelement %arg0{{.*}} : vector<2xf32>
// CHECK: %[[HALF1:.*]] = llvm.fptrunc %[[LANE1]] : f32 to f16
// CHECK: %[[BITS1:.*]] = llvm.bitcast %[[HALF1]] : f16 to i16
// CHECK: %[[HIGH:.*]] = llvm.zext %[[BITS1]] : i16 to i32
// CHECK: %[[SIXTEEN:.*]] = llvm.mlir.constant(16 : i32) : i32
// CHECK: %[[SHIFTED:.*]] = llvm.shl %[[HIGH]], %[[SIXTEEN]] : i32
// CHECK: %[[RES:.*]] = llvm.or %[[LOW]], %[[SHIFTED]] : i32
// CHECK: llvm.return %[[RES]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @pack_half2x16(%x: vector<2xf32>) -> (i32) "None" {
    %0 = spirv.GL.PackHalf2x16 %x : vector<2xf32> -> i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @pack_half2x16
  spirv.ExecutionMode @pack_half2x16 "LocalSize", 1, 1, 1
}

// -----

// `UnpackHalf2x16(v)` is the reverse: split `v` into its low 16 bits
// (lane 0) and high 16 bits (lane 1, via `lshr` by 16), reinterpret each
// as `f16`, then extend each to `f32`.
// CHECK-LABEL: llvm.func @unpack_half2x16
// CHECK: %[[SIXTEEN:.*]] = llvm.mlir.constant(16 : i32) : i32
// CHECK: %[[HIGHBITS32:.*]] = llvm.lshr %arg0, %[[SIXTEEN]] : i32
// CHECK: %[[BITS0:.*]] = llvm.trunc %arg0 : i32 to i16
// CHECK: %[[HALF0:.*]] = llvm.bitcast %[[BITS0]] : i16 to f16
// CHECK: %[[LOW:.*]] = llvm.fpext %[[HALF0]] : f16 to f32
// CHECK: %[[BITS1:.*]] = llvm.trunc %[[HIGHBITS32]] : i32 to i16
// CHECK: %[[HALF1:.*]] = llvm.bitcast %[[BITS1]] : i16 to f16
// CHECK: %[[HIGH:.*]] = llvm.fpext %[[HALF1]] : f16 to f32
// CHECK: %[[V0:.*]] = llvm.mlir.poison : vector<2xf32>
// CHECK: %[[V1:.*]] = llvm.insertelement %[[LOW]], %[[V0]]{{.*}} : vector<2xf32>
// CHECK: %[[V2:.*]] = llvm.insertelement %[[HIGH]], %[[V1]]{{.*}} : vector<2xf32>
// CHECK: llvm.return %[[V2]] : vector<2xf32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @unpack_half2x16(%v: i32) -> (vector<2xf32>) "None" {
    %0 = spirv.GL.UnpackHalf2x16 %v : i32 -> vector<2xf32>
    spirv.ReturnValue %0 : vector<2xf32>
  }
  spirv.EntryPoint "GLCompute" @unpack_half2x16
  spirv.ExecutionMode @unpack_half2x16 "LocalSize", 1, 1, 1
}
