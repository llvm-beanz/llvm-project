; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --groups=1,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s

; The same shader as `thread-id-store.ll`, but declaring its stage only with
; the source-independent `feme.shader.stage` attribute ("Stage identity" in
; feme/docs/FeMeGraphicsDesign.md) rather than DXIL's `hlsl.shader`. Entry
; point identity is `feme::isShaderEntryPoint`, which accepts either, so the
; whole JIT path -- entry selection, wave sizing, the CPU pipeline and the
; dispatch wrapper -- finds this shader just as it finds a raised one.

; CHECK: heap[0]: 0 1 2 3

;--- shader.ll
define void @main() #0 {
  %h = call target("dx.RawBuffer", i8, 1, 0)
      @llvm.dx.resource.handlefromheap(i32 0, i1 false)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %offset = mul i32 %tid, 4
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i8, 1, 0) %h, i32 %offset, i32 poison, i32 %tid)
  ret void
}
declare target("dx.RawBuffer", i8, 1, 0)
    @llvm.dx.resource.handlefromheap(i32, i1)
declare void @llvm.dx.resource.store.rawbuffer.i32(
    target("dx.RawBuffer", i8, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "feme.shader.stage"="compute" "hlsl.numthreads"="4,1,1" }

;--- heap.yaml
resource-heap:
  - index: 0
    size: 16
