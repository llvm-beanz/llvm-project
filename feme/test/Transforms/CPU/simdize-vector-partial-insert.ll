; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A divergent vector built by an `insertelement` chain over a *non-poison*
; base -- only one component depends on the lane, the other three come from
; the base constant -- must carry the base's own components into the
; decomposed per-component form. Before this was handled,
; `widenInsertElement` seeded every un-inserted component with null and the
; scalarized store silently passed `poison` for them.

; CHECK-LABEL: define void @main(
; CHECK: %[[V0:.*]] = insertelement <4 x float> poison, float %{{.*}}, i32 0
; CHECK: %[[V1:.*]] = insertelement <4 x float> %[[V0]], float 2.000000e+00, i32 1
; CHECK: %[[V2:.*]] = insertelement <4 x float> %[[V1]], float 3.000000e+00, i32 2
; CHECK: %[[V3:.*]] = insertelement <4 x float> %[[V2]], float 4.000000e+00, i32 3
; CHECK: call void @feme.cpu.resource.store.typed.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 {{%.*}}, <4 x float> %[[V3]], i1 {{%.*}})
define void @main() #0 {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefromheap.tdx.TypedBuffer_v4f32_1_0_0t(i32 0, i1 false)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %f = sitofp i32 %tid to float
  %v = insertelement <4 x float> <float 1.0, float 2.0, float 3.0, float 4.0>, float %f, i32 0
  call void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %tid, <4 x float> %v)
  ret void
}
declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
    @llvm.dx.resource.handlefromheap.tdx.TypedBuffer_v4f32_1_0_0t(i32, i1)
declare void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_0t.v4f32(
    target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32, <4 x float>)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
