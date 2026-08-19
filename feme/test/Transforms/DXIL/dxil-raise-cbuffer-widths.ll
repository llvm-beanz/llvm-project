; REQUIRES: directx-registered-target
; RUN: opt -S -dxil-op-lower %s | feme-opt --llvm -passes=feme-dxil-raise-ops -S | FileCheck %s

; End-to-end validation of feme::dxil::OpRaisingPass::raiseCBufferLoadLegacy's
; two non-32-bit `%dx.types.CBufRet.*` row overloads (`getCBufferRowIntrinsic`
; in OpRaising.cpp): starts from the pre-lowering
; `llvm.dx.resource.load.cbufferrow.{2,8}` intrinsic calls a real
; `-enable-16bit-types`/`double`-using DXIL frontend would emit, lowers them
; with the real `-dxil-op-lower` pass (which already lowers every
; `cbufferrow` width, see DXILOpLowering.cpp's `lowerCBufferLoad`), then
; raises the result back and checks it round-trips completely. Before this,
; only the 4-field 32-bit row (dxil-raise-resource-handles-roundtrip.ll's
; `cbuffer_case`) raised; a `cbuffer` of `half` scalars -- what a real
; `Texture2D`/`RWTexture2D`-and-`cbuffer` compute shader compiled with
; `dxc -T cs_6_2 -enable-16bit-types` produces -- left its `dx.CBuffer`
; handle entirely unraised, which `feme::amdgpu::ResourceLoweringPass`
; cannot model (see feme-dxil-to-amdgpu-unsupported-resource.ll).

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

%__cblayout_CB16 = type { half, half }
%__cblayout_CB64 = type { double }

@ResName16 = private unnamed_addr constant [4 x i8] c"cb1\00"
@ResName64 = private unnamed_addr constant [4 x i8] c"cb2\00"

; A `cbuffer` of `half` scalars: the 8-field `%dx.types.CBufRet.f16.8`
; overload raises to `llvm.dx.resource.load.cbufferrow.8`.
; CHECK-LABEL: define half @cbuffer_half(
define half @cbuffer_half(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.CBuffer", [4 x i8]) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: [[ROW:%.*]] = call { half, half, half, half, half, half, half, half } @llvm.dx.resource.load.cbufferrow.8{{.*}}(target("dx.CBuffer", [4 x i8]) [[HANDLE]], i32 %idx)
  ; CHECK: extractvalue { half, half, half, half, half, half, half, half } [[ROW]], 0
  ; CHECK: extractvalue { half, half, half, half, half, half, half, half } [[ROW]], 1
  %h = call target("dx.CBuffer", %__cblayout_CB16)
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_s_struct.CB16s_t(i32 0, i32 0, i32 1, i32 0, ptr @ResName16)
  %row = call {half,half,half,half,half,half,half,half} @llvm.dx.resource.load.cbufferrow.8.f16.tdx.CBuffer_s_struct.CB16s_t(target("dx.CBuffer", %__cblayout_CB16) %h, i32 %idx)
  %f0 = extractvalue {half,half,half,half,half,half,half,half} %row, 0
  %f1 = extractvalue {half,half,half,half,half,half,half,half} %row, 1
  %sum = fadd half %f0, %f1
  ret half %sum
}

; A `cbuffer` of a `double` scalar: the 2-field `%dx.types.CBufRet.f64.2`
; overload raises to `llvm.dx.resource.load.cbufferrow.2`.
; CHECK-LABEL: define double @cbuffer_double(
define double @cbuffer_double(i32 %idx) {
  ; CHECK: [[HANDLE:%.*]] = call target("dx.CBuffer", [8 x i8]) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: [[ROW:%.*]] = call { double, double } @llvm.dx.resource.load.cbufferrow.2{{.*}}(target("dx.CBuffer", [8 x i8]) [[HANDLE]], i32 %idx)
  ; CHECK: extractvalue { double, double } [[ROW]], 0
  %h = call target("dx.CBuffer", %__cblayout_CB64)
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_s_struct.CB64s_t(i32 0, i32 1, i32 1, i32 0, ptr @ResName64)
  %row = call {double,double} @llvm.dx.resource.load.cbufferrow.2.f64.tdx.CBuffer_s_struct.CB64s_t(target("dx.CBuffer", %__cblayout_CB64) %h, i32 %idx)
  %f0 = extractvalue {double,double} %row, 0
  ret double %f0
}
