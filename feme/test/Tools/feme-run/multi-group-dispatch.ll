; RUN: split-file %s %t
; RUN: feme-run --wave-size=4 --groups=2,1,1 --heap=%t/heap.yaml %t/shader.ll | FileCheck %s

; Two groups of 4 threads each (8 threads total): each group's dispatch
; thread id spans a distinct range, so this exercises `--groups` and
; `feme::cpu::JITEngine::dispatch`'s per-group `GroupID` plumbing, not just
; a single-group dispatch.

; CHECK: heap[0]: 0 1 2 3 4 5 6 7

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
    size: 32
