// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap L119: `spirv.GL.PackSnorm4x8`/`spirv.GL.UnpackSnorm4x8` already
// had a TableGen op definition but no conversion pattern at all before
// this fix -- neither this file's own `populateSPIRVToLLVMTargetPatterns`
// nor upstream MLIR's own `populateSPIRVToLLVMConversionPatterns`
// registered one, so both failed pipeline creation with "failed to
// legalize operation ... that was explicitly marked illegal".
// `PackUnorm4x8`/`PackUnorm2x16`/`UnpackUnorm2x16`/`UnpackUnorm4x8` had no
// op definition at all until this same fix added one.

// `PackSnorm4x8(v)` converts each lane `c` of `v` to fixed point via
// `round(clamp(c, -1, +1) * 127)`, then packs the four 8-bit results with
// lane 0 in the low byte and lane 3 in the high byte.
// CHECK-LABEL: llvm.func @pack_snorm_4x8
// CHECK: %[[NEGONE:.*]] = llvm.mlir.constant(-1.000000e+00 : f32) : f32
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1.000000e+00 : f32) : f32
// CHECK: %[[SCALE:.*]] = llvm.mlir.constant(1.270000e+02 : f32) : f32
// CHECK: %[[MASK:.*]] = llvm.mlir.constant(255 : i32) : i32
// CHECK: %[[LANE0:.*]] = llvm.extractelement %arg0{{.*}} : vector<4xf32>
// CHECK: %[[CLAMP0LO:.*]] = llvm.intr.maxnum(%[[LANE0]], %[[NEGONE]]) : (f32, f32) -> f32
// CHECK: %[[CLAMP0:.*]] = llvm.intr.minnum(%[[CLAMP0LO]], %[[ONE]]) : (f32, f32) -> f32
// CHECK: %[[SCALED0:.*]] = llvm.fmul %[[CLAMP0]], %[[SCALE]] : f32
// CHECK: %[[ROUNDED0:.*]] = llvm.intr.round(%[[SCALED0]]) : (f32) -> f32
// CHECK: %[[INT0:.*]] = llvm.fptosi %[[ROUNDED0]] : f32 to i32
// CHECK: %[[MASKED0:.*]] = llvm.and %[[INT0]], %[[MASK]] : i32
// CHECK: %[[SHIFT0:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[LOW:.*]] = llvm.shl %[[MASKED0]], %[[SHIFT0]] : i32
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @pack_snorm_4x8(%x: vector<4xf32>) -> (i32) "None" {
    %0 = spirv.GL.PackSnorm4x8 %x : vector<4xf32> -> i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @pack_snorm_4x8
  spirv.ExecutionMode @pack_snorm_4x8 "LocalSize", 1, 1, 1
}

// -----

// `UnpackSnorm4x8(p)` is the reverse: extract each of the four signed
// 8-bit fields of `p` (lane 0 from the low byte), convert to fixed point
// via `clamp(f / 127, -1, +1)`.
// CHECK-LABEL: llvm.func @unpack_snorm_4x8
// CHECK: %[[SCALE:.*]] = llvm.mlir.constant(1.270000e+02 : f32) : f32
// CHECK: %[[NEGONE:.*]] = llvm.mlir.constant(-1.000000e+00 : f32) : f32
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1.000000e+00 : f32) : f32
// CHECK: %[[SHL:.*]] = llvm.mlir.constant(24 : i32) : i32
// CHECK: %[[SHIFTED:.*]] = llvm.shl %arg0, %[[SHL]] : i32
// CHECK: %[[SHR:.*]] = llvm.mlir.constant(24 : i32) : i32
// CHECK: %[[EXTRACTED:.*]] = llvm.ashr %[[SHIFTED]], %[[SHR]] : i32
// CHECK: %[[ASFLOAT:.*]] = llvm.sitofp %[[EXTRACTED]] : i32 to f32
// CHECK: %[[NORM:.*]] = llvm.fdiv %[[ASFLOAT]], %[[SCALE]] : f32
// CHECK: %[[CLAMPLO:.*]] = llvm.intr.maxnum(%[[NORM]], %[[NEGONE]]) : (f32, f32) -> f32
// CHECK: %[[CLAMPED:.*]] = llvm.intr.minnum(%[[CLAMPLO]], %[[ONE]]) : (f32, f32) -> f32
// CHECK: llvm.insertelement %[[CLAMPED]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @unpack_snorm_4x8(%p: i32) -> (vector<4xf32>) "None" {
    %0 = spirv.GL.UnpackSnorm4x8 %p : i32 -> vector<4xf32>
    spirv.ReturnValue %0 : vector<4xf32>
  }
  spirv.EntryPoint "GLCompute" @unpack_snorm_4x8
  spirv.ExecutionMode @unpack_snorm_4x8 "LocalSize", 1, 1, 1
}

// -----

// `PackUnorm4x8(v)` is the unsigned encoding of the same shape: `round(
// clamp(c, 0, +1) * 255)` per lane, no clamp lower bound below zero.
// CHECK-LABEL: llvm.func @pack_unorm_4x8
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0.000000e+00 : f32) : f32
// CHECK: %[[ONE:.*]] = llvm.mlir.constant(1.000000e+00 : f32) : f32
// CHECK: %[[SCALE:.*]] = llvm.mlir.constant(2.550000e+02 : f32) : f32
// CHECK: %[[MASK:.*]] = llvm.mlir.constant(255 : i32) : i32
// CHECK: %[[LANE0:.*]] = llvm.extractelement %arg0{{.*}} : vector<4xf32>
// CHECK: %[[CLAMP0LO:.*]] = llvm.intr.maxnum(%[[LANE0]], %[[ZERO]]) : (f32, f32) -> f32
// CHECK: %[[CLAMP0:.*]] = llvm.intr.minnum(%[[CLAMP0LO]], %[[ONE]]) : (f32, f32) -> f32
// CHECK: %[[SCALED0:.*]] = llvm.fmul %[[CLAMP0]], %[[SCALE]] : f32
// CHECK: %[[ROUNDED0:.*]] = llvm.intr.round(%[[SCALED0]]) : (f32) -> f32
// CHECK: %[[INT0:.*]] = llvm.fptosi %[[ROUNDED0]] : f32 to i32
// CHECK: %[[MASKED0:.*]] = llvm.and %[[INT0]], %[[MASK]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @pack_unorm_4x8(%x: vector<4xf32>) -> (i32) "None" {
    %0 = spirv.GL.PackUnorm4x8 %x : vector<4xf32> -> i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @pack_unorm_4x8
  spirv.ExecutionMode @pack_unorm_4x8 "LocalSize", 1, 1, 1
}

// -----

// `UnpackUnorm4x8(p)` is the reverse: extract each unsigned 8-bit field
// (via `lshr`, not `ashr`) and divide by 255 -- no clamp needed since the
// unsigned encoding's own range is exactly `[0, 255]`.
// CHECK-LABEL: llvm.func @unpack_unorm_4x8
// CHECK: %[[SCALE:.*]] = llvm.mlir.constant(2.550000e+02 : f32) : f32
// CHECK: %[[SHL:.*]] = llvm.mlir.constant(24 : i32) : i32
// CHECK: %[[SHIFTED:.*]] = llvm.shl %arg0, %[[SHL]] : i32
// CHECK: %[[SHR:.*]] = llvm.mlir.constant(24 : i32) : i32
// CHECK: %[[EXTRACTED:.*]] = llvm.lshr %[[SHIFTED]], %[[SHR]] : i32
// CHECK: %[[ASFLOAT:.*]] = llvm.uitofp %[[EXTRACTED]] : i32 to f32
// CHECK: %[[NORM:.*]] = llvm.fdiv %[[ASFLOAT]], %[[SCALE]] : f32
// CHECK: llvm.insertelement %[[NORM]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @unpack_unorm_4x8(%p: i32) -> (vector<4xf32>) "None" {
    %0 = spirv.GL.UnpackUnorm4x8 %p : i32 -> vector<4xf32>
    spirv.ReturnValue %0 : vector<4xf32>
  }
  spirv.EntryPoint "GLCompute" @unpack_unorm_4x8
  spirv.ExecutionMode @unpack_unorm_4x8 "LocalSize", 1, 1, 1
}

// -----

// `PackUnorm2x16(v)`/`UnpackUnorm2x16(p)`: the same unsigned shape as
// `PackUnorm4x8`/`UnpackUnorm4x8` above, but 2 lanes of 16 bits each
// (scale 65535) instead of 4 lanes of 8 bits (scale 255).
// CHECK-LABEL: llvm.func @pack_unorm_2x16
// CHECK: %[[SCALE:.*]] = llvm.mlir.constant(6.553500e+04 : f32) : f32
// CHECK: %[[MASK:.*]] = llvm.mlir.constant(65535 : i32) : i32
// CHECK: llvm.extractelement %arg0{{.*}} : vector<2xf32>
// CHECK: llvm.fptosi
// CHECK: %[[MASKED0:.*]] = llvm.and {{.*}}, %[[MASK]] : i32
// CHECK: %[[SHIFT1:.*]] = llvm.mlir.constant(16 : i32) : i32
// CHECK: llvm.shl {{.*}}, %[[SHIFT1]] : i32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @pack_unorm_2x16(%x: vector<2xf32>) -> (i32) "None" {
    %0 = spirv.GL.PackUnorm2x16 %x : vector<2xf32> -> i32
    spirv.ReturnValue %0 : i32
  }
  spirv.EntryPoint "GLCompute" @pack_unorm_2x16
  spirv.ExecutionMode @pack_unorm_2x16 "LocalSize", 1, 1, 1
}

// -----

// CHECK-LABEL: llvm.func @unpack_unorm_2x16
// CHECK: %[[SCALE:.*]] = llvm.mlir.constant(6.553500e+04 : f32) : f32
// CHECK: %[[SHL0:.*]] = llvm.mlir.constant(16 : i32) : i32
// CHECK: llvm.shl %arg0, %[[SHL0]] : i32
// CHECK: %[[SHR:.*]] = llvm.mlir.constant(16 : i32) : i32
// CHECK: llvm.lshr {{.*}}, %[[SHR]] : i32
// CHECK: llvm.uitofp
// CHECK: llvm.fdiv {{.*}}, %[[SCALE]] : f32
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @unpack_unorm_2x16(%p: i32) -> (vector<2xf32>) "None" {
    %0 = spirv.GL.UnpackUnorm2x16 %p : i32 -> vector<2xf32>
    spirv.ReturnValue %0 : vector<2xf32>
  }
  spirv.EntryPoint "GLCompute" @unpack_unorm_2x16
  spirv.ExecutionMode @unpack_unorm_2x16 "LocalSize", 1, 1, 1
}
