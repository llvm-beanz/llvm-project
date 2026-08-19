; RUN: feme-opt --llvm -passes=feme-amdgpu-lower-resources -S %s | FileCheck %s

; feme::amdgpu::ResourceLoweringPass's `dx.Texture`/`dx.CBuffer` support
; (Roadmap.md's "the remaining resource access ops"/"RaisedLoweringPass
; breadth" P1 entries): a texture binding gets a flat `ptr addrspace(1)`
; data pointer plus one trailing `i32` row-pitch "stride" argument per
; coordinate beyond the first (`Binding::NumAuxArgs`), and a 2D texture
; access is linearized against it (`coord0 + coord1*stride0`) the same way
; an ordinary row-major array index would be; a cbuffer binding needs no
; such stride -- each row is always 16 bytes, so a row's field N is simply
; an `ElementType`-strided load starting at that 16-byte-aligned offset.
;
; `texture_load_store` also exercises `Binding::IsUAV`: its SRV (`t0`) and
; UAV (`u0`) bindings share the numeric pair (space 0, register 0), since
; HLSL's `t`/`u` registers are independent namespaces -- before `IsUAV`
; distinguished them, collectBindings incorrectly treated them as the same
; binding reached through two different element types, which aborted
; lowering this (or any) function's resources entirely.

target triple = "amdgcn-amd-amdhsa"

; CHECK: define <4 x half> @texture_load_store(<2 x i32> %coord, <4 x half> %val, ptr addrspace(1) [[IN:%.*]], i32 [[INSTRIDE:%.*]], ptr addrspace(1) [[OUT:%.*]], i32 [[OUTSTRIDE:%.*]])
; CHECK-NEXT: [[X:%.*]] = extractelement <2 x i32> %coord, i32 0
; CHECK-NEXT: [[Y:%.*]] = extractelement <2 x i32> %coord, i32 1
; CHECK-NEXT: [[YSTRIDE:%.*]] = mul i32 [[Y]], [[INSTRIDE]]
; CHECK-NEXT: [[IDX:%.*]] = add i32 [[X]], [[YSTRIDE]]
; CHECK-NEXT: [[ELEM:%.*]] = getelementptr <4 x half>, ptr addrspace(1) [[IN]], i32 [[IDX]]
; CHECK-NEXT: [[VAL:%.*]] = load <4 x half>, ptr addrspace(1) [[ELEM]], align 8
; CHECK-NEXT: [[X2:%.*]] = extractelement <2 x i32> %coord, i32 0
; CHECK-NEXT: [[Y2:%.*]] = extractelement <2 x i32> %coord, i32 1
; CHECK-NEXT: [[YSTRIDE2:%.*]] = mul i32 [[Y2]], [[OUTSTRIDE]]
; CHECK-NEXT: [[IDX2:%.*]] = add i32 [[X2]], [[YSTRIDE2]]
; CHECK-NEXT: [[ELEM2:%.*]] = getelementptr <4 x half>, ptr addrspace(1) [[OUT]], i32 [[IDX2]]
; CHECK-NEXT: store <4 x half> %val, ptr addrspace(1) [[ELEM2]], align 8
; CHECK-NEXT: ret <4 x half> [[VAL]]
define <4 x half> @texture_load_store(<2 x i32> %coord, <4 x half> %val) #0 {
  %in = call target("dx.Texture", <4 x half>, 0, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f16_0_0_1_2t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %out = call target("dx.Texture", <4 x half>, 1, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f16_1_0_1_2t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %loaded = call <4 x half> @llvm.dx.resource.load.level.v4f16.tdx.Texture_v4f16_0_0_1_2t.v2i32.i32.v2i32(
      target("dx.Texture", <4 x half>, 0, 0, 1, 2) %in, <2 x i32> %coord, i32 0, <2 x i32> zeroinitializer)
  call void @llvm.dx.resource.store.texture.tdx.Texture_v4f16_1_0_1_2t.v2i32.v4f16(
      target("dx.Texture", <4 x half>, 1, 0, 1, 2) %out, <2 x i32> %coord, <4 x half> %val)
  ret <4 x half> %loaded
}

; CHECK: define half @cbuffer_row(i32 %idx, ptr addrspace(1) [[CB:%.*]])
; CHECK-NEXT: [[BYTES:%.*]] = mul i32 %idx, 16
; CHECK-NEXT: [[ROW:%.*]] = getelementptr i8, ptr addrspace(1) [[CB]], i32 [[BYTES]]
; CHECK-NEXT: [[FIELD1PTR:%.*]] = getelementptr half, ptr addrspace(1) [[ROW]], i32 1
; CHECK-NEXT: [[FIELD1:%.*]] = load half, ptr addrspace(1) [[FIELD1PTR]], align 2
; CHECK-NEXT: ret half [[FIELD1]]
define half @cbuffer_row(i32 %idx) #0 {
  %cb = call target("dx.CBuffer", [4 x i8])
      @llvm.dx.resource.handlefrombinding.tdx.CBuffer_a4i8t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %row = call {half,half,half,half,half,half,half,half} @llvm.dx.resource.load.cbufferrow.8.f16.tdx.CBuffer_a4i8t(
      target("dx.CBuffer", [4 x i8]) %cb, i32 %idx)
  %f1 = extractvalue {half,half,half,half,half,half,half,half} %row, 1
  ret half %f1
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,8,1" }

; `GetDimensions`' raised `.x`/`.xy` accesses (`raiseGetDimensions` in
; OpRaising.cpp) each get their own dedicated trailing `i32` kernel
; argument(s) -- `Binding::NumDimensionArgs` -- appended after the binding's
; own addressing stride argument (`Binding::NumAuxArgs`, always present for
; a 2D `dx.Texture` binding regardless of whether it is also loaded/stored,
; hence the unused `%dims_x.stride0`/`%dims_xy.stride0` below): a `.x`-only
; access (`dims_x`) becomes a plain read of that one dedicated argument, and
; an `.xy` access (`dims_xy`) packs its two dedicated arguments into the
; `<2 x i32>` it returns.
; CHECK: define i32 @dims_x(ptr addrspace(1) {{%.*}}, i32 {{%.*}}, i32 [[W:%.*]])
; CHECK-NEXT: ret i32 [[W]]
define i32 @dims_x() #0 {
  %tex = call target("dx.Texture", <4 x half>, 0, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f16_0_0_1_2t(i32 0, i32 0, i32 1, i32 0, ptr null)
  %w = call i32 @llvm.dx.resource.getdimensions.x.tdx.Texture_v4f16_0_0_1_2t(
      target("dx.Texture", <4 x half>, 0, 0, 1, 2) %tex)
  ret i32 %w
}

; CHECK: define <2 x i32> @dims_xy(ptr addrspace(1) {{%.*}}, i32 {{%.*}}, i32 [[W2:%.*]], i32 [[H2:%.*]])
; CHECK-NEXT: [[V0:%.*]] = insertelement <2 x i32> poison, i32 [[W2]], i32 0
; CHECK-NEXT: [[V1:%.*]] = insertelement <2 x i32> [[V0]], i32 [[H2]], i32 1
; CHECK-NEXT: ret <2 x i32> [[V1]]
define <2 x i32> @dims_xy() #0 {
  %tex = call target("dx.Texture", <4 x half>, 1, 0, 1, 2)
      @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f16_1_0_1_2t(i32 0, i32 1, i32 1, i32 0, ptr null)
  %d = call <2 x i32> @llvm.dx.resource.getdimensions.xy.tdx.Texture_v4f16_1_0_1_2t(
      target("dx.Texture", <4 x half>, 1, 0, 1, 2) %tex)
  ret <2 x i32> %d
}

