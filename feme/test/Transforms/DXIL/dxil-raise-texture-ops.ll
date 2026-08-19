; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Covers feme::dxil::OpRaisingPass's texture/sampler access raising
; (raiseSample/raiseSampleLevel/raiseTextureLoad/raiseTextureStore/
; raiseGetDimensionsX in OpRaising.cpp): the texture/sampler handle
; reconstruction itself was the "only the implementation left" remainder of
; Design.md's "Decision: texture and sampler handle kinds" (roadmap R30);
; raiseTextureStore closes Roadmap.md's "the remaining resource access ops"
; P1 entry's texture-store half (the load half, raiseTextureLoad, already
; existed).
; Every handle below is bindless (`ResourceDescriptorHeap`/
; `SamplerDescriptorHeap`), exactly like dxil-raise-resource-heap-handles.ll,
; since the CPU target accepts bindless shaders only.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-pc-shadermodel6.6-compute"

%dx.types.Handle = type { ptr }
%dx.types.ResourceProperties = type { i32, i32 }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }
%dx.types.Dimensions = type { i32, i32, i32, i32 }

; A `Texture2D<float4>` (kind 2) implicit-LOD sample through a bindless
; `Sampler` (kind 14): `dx.op.sample` (60) raises to `llvm.dx.resource.
; sample`, reassembling `Coord0`/`Coord1` (the only two meaningful
; components for a 2D texture) into a `<2 x float>` and the (both-zero, so
; constant-folded) `Offset0`/`Offset1` into a `<2 x i32>`; `Coord2`/`Coord3`/
; `Offset2`/`Clamp` are `poison`/unused padding DXIL always carries in this
; op's fixed-arity encoding.
; CHECK-LABEL: define <4 x float> @sample_2d(
define <4 x float> @sample_2d(i32 %idx, i32 %sampidx, float %u, float %v) {
  ; CHECK: [[TEX:%.*]] = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  ; CHECK: [[SAMP:%.*]] = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap{{.*}}(i32 %sampidx, i1 false)
  ; CHECK: [[C0:%.*]] = insertelement <2 x float> poison, float %u, i32 0
  ; CHECK: [[C1:%.*]] = insertelement <2 x float> [[C0]], float %v, i32 1
  ; CHECK: [[RES:%.*]] = call <4 x float> @llvm.dx.resource.sample{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 2) [[TEX]], target("dx.Sampler", 0) [[SAMP]], <2 x float> [[C1]], <2 x i32> zeroinitializer)
  ; CHECK: extractelement <4 x float> [[RES]], i64 0
  ; CHECK: extractelement <4 x float> [[RES]], i64 1
  ; CHECK: extractelement <4 x float> [[RES]], i64 2
  ; CHECK: extractelement <4 x float> [[RES]], i64 3
  %tex1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %tex2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %tex1, %dx.types.ResourceProperties { i32 2, i32 1033 })
  %samp1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %sampidx, i1 true, i1 false)
  %samp2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %samp1, %dx.types.ResourceProperties { i32 14, i32 0 })
  %r = call %dx.types.ResRet.f32 @dx.op.sample.f32(i32 60, %dx.types.Handle %tex2, %dx.types.Handle %samp2, float %u, float %v, float poison, float poison, i32 0, i32 0, i32 poison, float poison)
  %r0 = extractvalue %dx.types.ResRet.f32 %r, 0
  %r1 = extractvalue %dx.types.ResRet.f32 %r, 1
  %r2 = extractvalue %dx.types.ResRet.f32 %r, 2
  %r3 = extractvalue %dx.types.ResRet.f32 %r, 3
  %v0 = insertelement <4 x float> poison, float %r0, i32 0
  %v1 = insertelement <4 x float> %v0, float %r1, i32 1
  %v2 = insertelement <4 x float> %v1, float %r2, i32 2
  %v3 = insertelement <4 x float> %v2, float %r3, i32 3
  ret <4 x float> %v3
}

; A `Texture2D<float4>` explicit-LOD sample: `dx.op.sampleLevel` (62) raises
; to `llvm.dx.resource.samplelevel`, carrying the LOD operand through
; unchanged (unlike `Sample`, `SampleLevel` has no trailing `Clamp` operand
; at all -- see DXIL.td).
; CHECK-LABEL: define <4 x float> @samplelevel_2d(
define <4 x float> @samplelevel_2d(i32 %idx, i32 %sampidx, float %u, float %v, float %lod) {
  ; CHECK: [[TEX:%.*]] = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  ; CHECK: [[SAMP:%.*]] = call target("dx.Sampler", 0) @llvm.dx.resource.handlefromheap{{.*}}(i32 %sampidx, i1 false)
  ; CHECK: [[RES:%.*]] = call <4 x float> @llvm.dx.resource.samplelevel{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 2) [[TEX]], target("dx.Sampler", 0) [[SAMP]], <2 x float> {{.*}}, float %lod, <2 x i32> zeroinitializer)
  %tex1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %tex2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %tex1, %dx.types.ResourceProperties { i32 2, i32 1033 })
  %samp1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %sampidx, i1 true, i1 false)
  %samp2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %samp1, %dx.types.ResourceProperties { i32 14, i32 0 })
  %r = call %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32 62, %dx.types.Handle %tex2, %dx.types.Handle %samp2, float %u, float %v, float poison, float poison, i32 0, i32 0, i32 poison, float %lod)
  %r0 = extractvalue %dx.types.ResRet.f32 %r, 0
  %r1 = extractvalue %dx.types.ResRet.f32 %r, 1
  %r2 = extractvalue %dx.types.ResRet.f32 %r, 2
  %r3 = extractvalue %dx.types.ResRet.f32 %r, 3
  %v0 = insertelement <4 x float> poison, float %r0, i32 0
  %v1 = insertelement <4 x float> %v0, float %r1, i32 1
  %v2 = insertelement <4 x float> %v1, float %r2, i32 2
  %v3 = insertelement <4 x float> %v2, float %r3, i32 3
  ret <4 x float> %v3
}

; A `Texture2D<float4>` explicit-mip texel fetch (no sampler): `dx.op.
; textureLoad` (66) raises to `llvm.dx.resource.load.level`, whose integer
; coordinates need no separate vector-width helper -- `TextureLoad`'s own
; 3-wide `Coord0..2`/`Offset0..2` are simply truncated to 2 components for a
; `Texture2D`.
; CHECK-LABEL: define <4 x float> @textureload_2d(
define <4 x float> @textureload_2d(i32 %idx, i32 %x, i32 %y, i32 %mip) {
  ; CHECK: [[TEX:%.*]] = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  ; CHECK: [[RES:%.*]] = call <4 x float> @llvm.dx.resource.load.level{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 2) [[TEX]], <2 x i32> {{.*}}, i32 %mip, <2 x i32> zeroinitializer)
  %tex1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %tex2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %tex1, %dx.types.ResourceProperties { i32 2, i32 1033 })
  %r = call %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32 66, %dx.types.Handle %tex2, i32 %mip, i32 %x, i32 %y, i32 poison, i32 0, i32 0, i32 poison)
  %r0 = extractvalue %dx.types.ResRet.f32 %r, 0
  %r1 = extractvalue %dx.types.ResRet.f32 %r, 1
  %r2 = extractvalue %dx.types.ResRet.f32 %r, 2
  %r3 = extractvalue %dx.types.ResRet.f32 %r, 3
  %v0 = insertelement <4 x float> poison, float %r0, i32 0
  %v1 = insertelement <4 x float> %v0, float %r1, i32 1
  %v2 = insertelement <4 x float> %v1, float %r2, i32 2
  %v3 = insertelement <4 x float> %v2, float %r3, i32 3
  ret <4 x float> %v3
}

; `dx.op.getDimensions` (72)'s `.x` (width) field raises to `llvm.dx.
; resource.getdimensions.x`; the `.y` (height) field is left as an
; unmodified `extractvalue` since no `getdimensions.xy` lowering exists yet
; to cross-check against (see the section header comment in OpRaising.cpp).
; CHECK-LABEL: define i32 @dimensions_2d(
define i32 @dimensions_2d(i32 %idx) {
  ; CHECK: [[TEX:%.*]] = call target("dx.Texture", <4 x float>, 0, 0, 1, 2) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  ; CHECK: [[W:%.*]] = call i32 @llvm.dx.resource.getdimensions.x{{.*}}(target("dx.Texture", <4 x float>, 0, 0, 1, 2) [[TEX]])
  ; CHECK: ret i32 [[W]]
  %tex1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %tex2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %tex1, %dx.types.ResourceProperties { i32 2, i32 1033 })
  %d = call %dx.types.Dimensions @dx.op.getDimensions(i32 72, %dx.types.Handle %tex2, i32 poison)
  %w = extractvalue %dx.types.Dimensions %d, 0
  ret i32 %w
}

; A writeable `RWTexture2D<float4>` (kind 2, `IsUAV`) texel write:
; `dx.op.textureStore` (67) raises to `llvm.dx.resource.store.texture`,
; reassembling `Val0..3` into a `<4 x float>` (mirroring how
; `raiseTypedBufferStore` reassembles `BufferStore`'s) and truncating
; `Coord0..2` to 2 components for a `Texture2D`, the same way
; `textureload_2d` above truncates `TextureLoad`'s.
; CHECK-LABEL: define void @texturestore_2d(
define void @texturestore_2d(i32 %idx, i32 %x, i32 %y, <4 x float> %v) {
  ; CHECK: [[TEX:%.*]] = call target("dx.Texture", <4 x float>, 1, 0, 1, 2) @llvm.dx.resource.handlefromheap{{.*}}(i32 %idx, i1 false)
  ; CHECK: [[COORD:%.*]] = insertelement <2 x i32> poison, i32 %x, i32 0
  ; CHECK: insertelement <2 x i32> [[COORD]], i32 %y, i32 1
  ; CHECK: call void @llvm.dx.resource.store.texture{{.*}}(target("dx.Texture", <4 x float>, 1, 0, 1, 2) [[TEX]], <2 x i32> {{.*}}, <4 x float> {{.*}})
  %tex1 = call %dx.types.Handle @dx.op.createHandleFromHeap(i32 218, i32 %idx, i1 false, i1 false)
  %tex2 = call %dx.types.Handle @dx.op.annotateHandle(i32 216, %dx.types.Handle %tex1, %dx.types.ResourceProperties { i32 4098, i32 1033 })
  %v0 = extractelement <4 x float> %v, i32 0
  %v1 = extractelement <4 x float> %v, i32 1
  %v2 = extractelement <4 x float> %v, i32 2
  %v3 = extractelement <4 x float> %v, i32 3
  call void @dx.op.textureStore.f32(i32 67, %dx.types.Handle %tex2, i32 %x, i32 %y, i32 poison, float %v0, float %v1, float %v2, float %v3, i8 15)
  ret void
}

declare %dx.types.Handle @dx.op.createHandleFromHeap(i32, i32, i1, i1)
declare %dx.types.Handle @dx.op.annotateHandle(i32, %dx.types.Handle, %dx.types.ResourceProperties)
declare %dx.types.ResRet.f32 @dx.op.sample.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float)
declare %dx.types.ResRet.f32 @dx.op.sampleLevel.f32(i32, %dx.types.Handle, %dx.types.Handle, float, float, float, float, i32, i32, i32, float)
declare %dx.types.ResRet.f32 @dx.op.textureLoad.f32(i32, %dx.types.Handle, i32, i32, i32, i32, i32, i32, i32)
declare void @dx.op.textureStore.f32(i32, %dx.types.Handle, i32, i32, i32, float, float, float, float, i8)
declare %dx.types.Dimensions @dx.op.getDimensions(i32, %dx.types.Handle, i32)
