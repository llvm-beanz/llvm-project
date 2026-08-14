; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A divergent, vector-typed resource *load* (a typed-buffer element, e.g.
; `<4 x float>`) is the second producer shape "Vectors become components,
; not nested vectors" ("Phase 4: Widening") supports (see
; `checkVectorDecompositionSupported`'s file comment): it decomposes into
; one `<W x float>` per component directly as `widenResourceCall`
; scalarizes it, instead of one illegal `<W x <4 x float>>`.
; `extractelement` (see simdize-vector-extractelement.ll) reads each
; component straight back out.

; CHECK-LABEL: define void @main(
; CHECK-NOT: <4 x <4 x float>>
; CHECK-COUNT-4: call <4 x float> @feme.cpu.resource.load.typed.v4f32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 {{%.*}}, i1 {{%.*}})
; CHECK: fadd <4 x float>
define void @main() #0 {
  %h = call target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
      @llvm.dx.resource.handlefromheap.tdx.TypedBuffer_v4f32_1_0_0t(i32 0, i1 false)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %r = call {<4 x float>, i1} @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_1_0_0t(
      target("dx.TypedBuffer", <4 x float>, 1, 0, 0) %h, i32 %tid)
  %v = extractvalue {<4 x float>, i1} %r, 0
  %e0 = extractelement <4 x float> %v, i32 0
  %e2 = extractelement <4 x float> %v, i32 2
  %sum = fadd float %e0, %e2
  ret void
}
declare target("dx.TypedBuffer", <4 x float>, 1, 0, 0)
    @llvm.dx.resource.handlefromheap.tdx.TypedBuffer_v4f32_1_0_0t(i32, i1)
declare {<4 x float>, i1} @llvm.dx.resource.load.typedbuffer.v4f32.tdx.TypedBuffer_v4f32_1_0_0t(
    target("dx.TypedBuffer", <4 x float>, 1, 0, 0), i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
