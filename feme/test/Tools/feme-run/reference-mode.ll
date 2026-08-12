; RUN: split-file %s %t
; RUN: feme-run --reference --groups=1,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s

; `--reference` (see the "CFG restructurization test suite" section of
; feme/docs/FeMeCPUDesign.md) runs the shader one invocation at a time
; through the unwidened module: this is the same shader/heap
; thread-id-store.ll uses to exercise the normal (widened) path, so the
; expected output is identical -- `--reference` is meant to be
; indistinguishable from the ground truth it diffs against on a
; wave-size-independent shader like this one.

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
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }

;--- heap.yaml
resource-heap:
  - index: 0
    size: 16
