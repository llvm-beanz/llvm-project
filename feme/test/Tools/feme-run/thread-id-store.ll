; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --groups=1,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s

; feme-run JITs a raised shader and dispatches it against a small textual
; heap description, then prints the resulting heap contents -- this is the
; tool that turns "does this translate correctly?" into "does this compute
; the right answer?" (see "Command line" in feme/docs/FeMeCPUDesign.md).
; This shader writes its own dispatch thread id to a raw buffer at that
; same (word) index; with 4 threads and W = 4, one wave covers the whole
; group.

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
