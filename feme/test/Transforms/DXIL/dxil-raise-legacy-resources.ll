; RUN: feme-opt --llvm -passes=feme-dxil-raise-ops -S %s | FileCheck %s

; Pre-SM6.6 DXIL binds resources with the legacy `dx.op.createHandle` op (57),
; which names its resource indirectly by (resource class, range ID) -- an
; index into the module's `!dx.resources` metadata -- rather than carrying the
; binding inline the way `dx.op.createHandleFromBinding` (217) does. This
; checks that feme::dxil::OpRaisingPass reads that metadata back to
; reconstruct `llvm.dx.resource.handlefrombinding`, and that the typed buffer
; loads/stores consuming those handles are raised too, so no
; `%dx.types.Handle` bridge is left behind. This is the shape a real `dxc
; -T cs_6_5` compilation produces; see the DXIL section of
; feme/docs/Design.md.

target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:32-i32:32-i64:64-f16:32-f32:32-f64:64-n8:16:32:64"
target triple = "dxil-ms-dx"

%dx.types.Handle = type { ptr }
%dx.types.ResRet.f32 = type { float, float, float, float, i32 }

; A `RWBuffer<float4>` at u0, space 0. DXIL metadata records only the *scalar*
; component type (F32), so the `<4 x float>` element type is recovered from the
; store's write mask (15 = all four components).
; CHECK-LABEL: define void @store_float4(
define void @store_float4(i32 %idx, float %x, float %y, float %z, float %w) {
  ; CHECK: [[H:%.*]] = call target("dx.TypedBuffer", <4 x float>, 1, 0, 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 0, i32 1, i32 0, ptr null)
  ; CHECK: [[V0:%.*]] = insertelement <4 x float> poison, float %x, i32 0
  ; CHECK: [[V1:%.*]] = insertelement <4 x float> [[V0]], float %y, i32 1
  ; CHECK: [[V2:%.*]] = insertelement <4 x float> [[V1]], float %z, i32 2
  ; CHECK: [[V3:%.*]] = insertelement <4 x float> [[V2]], float %w, i32 3
  ; CHECK: call void @llvm.dx.resource.store.typedbuffer{{.*}}(target("dx.TypedBuffer", <4 x float>, 1, 0, 1) [[H]], i32 %idx, <4 x float> [[V3]])
  ; CHECK-NOT: casthandle
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 1, i32 0, i32 0, i1 false)
  call void @dx.op.bufferStore.f32(i32 69, %dx.types.Handle %h, i32 %idx, i32 undef, float %x, float %y, float %z, float %w, i8 15)
  ret void
}

; A `Buffer<float>` (SRV) at t1, space 0: a single-component typed buffer,
; recovered from the load only ever extracting component 0. `createHandle`'s
; index operand is the absolute register index, so raising rebases it against
; the binding's lower bound.
; CHECK-LABEL: define float @load_float(
define float @load_float(i32 %idx) {
  ; CHECK: [[H:%.*]] = call target("dx.TypedBuffer", float, 0, 0, 1) @llvm.dx.resource.handlefrombinding{{.*}}(i32 0, i32 1, i32 1, i32 0, ptr null)
  ; CHECK: [[LOAD:%.*]] = call { float, i1 } @llvm.dx.resource.load.typedbuffer{{.*}}(target("dx.TypedBuffer", float, 0, 0, 1) [[H]], i32 %idx)
  ; CHECK: [[V:%.*]] = extractvalue { float, i1 } [[LOAD]], 0
  ; CHECK: ret float [[V]]
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 0, i32 1, i1 false)
  %v = call %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32 68, %dx.types.Handle %h, i32 %idx, i32 undef)
  %r = extractvalue %dx.types.ResRet.f32 %v, 0
  ret float %r
}

; A resource kind this pass doesn't reconstruct yet (a Texture2D SRV) is left
; as an unmodified `dx.op.*` call rather than being mis-raised.
; CHECK-LABEL: define void @unsupported_kind(
define void @unsupported_kind() {
  ; CHECK: call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 1, i32 2, i1 false)
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 0, i32 1, i32 2, i1 false)
  ret void
}

declare %dx.types.Handle @dx.op.createHandle(i32, i8, i32, i32, i1)
declare void @dx.op.bufferStore.f32(i32, %dx.types.Handle, i32, i32, float, float, float, float, i8)
declare %dx.types.ResRet.f32 @dx.op.bufferLoad.f32(i32, %dx.types.Handle, i32, i32)

!dx.resources = !{!0}

!0 = !{!1, !5, null, null}
; SRVs: {ID, GV, name, space, lowerBound, rangeSize, kind, sampleCount, props}
!1 = !{!2, !3}
!2 = !{i32 0, ptr undef, !"buf", i32 0, i32 1, i32 1, i32 10, i32 0, !4}
!3 = !{i32 1, ptr undef, !"tex2d", i32 0, i32 2, i32 1, i32 2, i32 0, !4}
!4 = !{i32 0, i32 9}
; UAVs: {ID, GV, name, space, lowerBound, rangeSize, kind, coherent, counter,
;        ROV, props}
!5 = !{!6}
!6 = !{i32 0, ptr undef, !"rwbuf", i32 0, i32 0, i32 1, i32 10, i1 false, i1 false, i1 false, !4}
