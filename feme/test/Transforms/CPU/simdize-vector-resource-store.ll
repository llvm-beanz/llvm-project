; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A divergent vector value built by a constant-index `insertelement` chain
; -- exactly the shape `feme::dxil::OpRaisingPass`'s `raiseTypedBufferStore`
; produces reassembling a typed-buffer store's four scalar components (see
; OpRaising.cpp) -- decomposes into one `<W x float>` per component instead
; of a single, illegal `<W x <4 x float>>` ("Vectors become components, not
; nested vectors" in "Phase 4: Widening"); the masked resource-store call
; consuming it rebuilds each lane's `<4 x float>` argument from those
; components rather than a single `extractelement`.

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK-COUNT-4: call void @feme.cpu.resource.store.typed.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 {{%.*}}, <4 x float> {{%.*}}, i1 {{%.*}})
define void @main() #0 {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 1)
      @llvm.dx.resource.handlefromheap.tdx.TypedBuffer_v4f32_1_0_1t(i32 0, i1 false)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %tidf = uitofp i32 %tid to float
  %v0 = insertelement <4 x float> poison, float %tidf, i32 0
  %v1 = insertelement <4 x float> %v0, float %tidf, i32 1
  %v2 = insertelement <4 x float> %v1, float %tidf, i32 2
  %v3 = insertelement <4 x float> %v2, float 1.000000e+00, i32 3
  call void @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_1t.v4f32(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 1) %h, i32 %tid, <4 x float> %v3)
  ret void
}
declare target("dx.TypedBuffer", <4 x float>, 1, 0, 1)
    @llvm.dx.resource.handlefromheap.tdx.TypedBuffer_v4f32_1_0_1t(i32, i1)
declare void
    @llvm.dx.resource.store.typedbuffer.tdx.TypedBuffer_v4f32_1_0_1t.v4f32(
        target("dx.TypedBuffer", <4 x float>, 1, 0, 1), i32, <4 x float>)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
