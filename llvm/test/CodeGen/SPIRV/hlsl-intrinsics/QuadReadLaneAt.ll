; RUN: llc -verify-machineinstrs -O0 -mtriple=spirv1.5-vulkan-unknown %s -o - | FileCheck %s
; RUN: %if spirv-tools %{ llc -O0 -mtriple=spirv1.5-vulkan-unknown %s -o - -filetype=obj | spirv-val %}

; Test lowering to spir-v backend for various types and scalar/vector

; CHECK: OpCapability GroupNonUniformQuad

; CHECK-DAG:   %[[#f16:]] = OpTypeFloat 16
; CHECK-DAG:   %[[#f32:]] = OpTypeFloat 32
; CHECK-DAG:   %[[#uint:]] = OpTypeInt 32 0
; CHECK-DAG:   %[[#v4_float:]] = OpTypeVector %[[#f32]] 4
; CHECK-DAG:   %[[#scope:]] = OpConstant %[[#uint]] 3

; CHECK-LABEL: Begin function test_float
; CHECK:   %[[#fexpr:]] = OpFunctionParameter %[[#f32]]
; CHECK:   %[[#idx1:]] = OpFunctionParameter %[[#uint]]
define float @test_float(float %fexpr, i32 %idx) {
entry:
; CHECK:   %[[#fret:]] = OpGroupNonUniformQuadBroadcast %[[#f32]] %[[#scope]] %[[#fexpr]] %[[#idx1]]
  %0 = call float @llvm.spv.quad.read.lane.at.f32(float %fexpr, i32 %idx)
  ret float %0
}

; CHECK-LABEL: Begin function test_int
; CHECK:   %[[#iexpr:]] = OpFunctionParameter %[[#uint]]
; CHECK:   %[[#idx2:]] = OpFunctionParameter %[[#uint]]
define i32 @test_int(i32 %iexpr, i32 %idx) {
entry:
; CHECK:   %[[#iret:]] = OpGroupNonUniformQuadBroadcast %[[#uint]] %[[#scope]] %[[#iexpr]] %[[#idx2]]
  %0 = call i32 @llvm.spv.quad.read.lane.at.i32(i32 %iexpr, i32 %idx)
  ret i32 %0
}

; CHECK-LABEL: Begin function test_half
; CHECK:   %[[#hexpr:]] = OpFunctionParameter %[[#f16]]
; CHECK:   %[[#idx3:]] = OpFunctionParameter %[[#uint]]
define half @test_half(half %hexpr, i32 %idx) {
entry:
; CHECK:   %[[#hret:]] = OpGroupNonUniformQuadBroadcast %[[#f16]] %[[#scope]] %[[#hexpr]] %[[#idx3]]
  %0 = call half @llvm.spv.quad.read.lane.at.f16(half %hexpr, i32 %idx)
  ret half %0
}

; CHECK-LABEL: Begin function test_vfloat
; CHECK:   %[[#vfexpr:]] = OpFunctionParameter %[[#v4_float]]
; CHECK:   %[[#idx4:]] = OpFunctionParameter %[[#uint]]
define <4 x float> @test_vfloat(<4 x float> %vfexpr, i32 %idx) {
entry:
; CHECK:   %[[#vfret:]] = OpGroupNonUniformQuadBroadcast %[[#v4_float]] %[[#scope]] %[[#vfexpr]] %[[#idx4]]
  %0 = call <4 x float> @llvm.spv.quad.read.lane.at.v4f32(<4 x float> %vfexpr, i32 %idx)
  ret <4 x float> %0
}

declare float @llvm.spv.quad.read.lane.at.f32(float, i32)
declare i32 @llvm.spv.quad.read.lane.at.i32(i32, i32)
declare half @llvm.spv.quad.read.lane.at.f16(half, i32)
declare <4 x float> @llvm.spv.quad.read.lane.at.v4f32(<4 x float>, i32)
