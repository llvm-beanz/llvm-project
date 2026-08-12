; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A `feme.cpu.resource.*` call whose offset is divergent (a thread id) is
; scalarized into one call per lane, extracting the widened offset/value and
; ANDing the wave's entry mask into each lane's call (see "masked
; feme.cpu.resource.* call" in "Phase 4: Widening" in
; feme/docs/FeMeCPUDesign.md); the constant descriptor index stays uniform
; and unwidened.

; CHECK-LABEL: define void @main(
; CHECK-COUNT-4: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0,
; CHECK-NOT: call void @feme.cpu.resource.store.raw.i32(
define void @main() #0 {
  %h = call target("dx.RawBuffer", i32, 1, 0) @llvm.dx.resource.handlefromheap(i32 0, i1 false)
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %offset = mul i32 %tid, 4
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i32, 1, 0) %h, i32 %tid, i32 %offset, i32 %tid)
  ret void
}
declare target("dx.RawBuffer", i32, 1, 0) @llvm.dx.resource.handlefromheap(i32, i1)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i32, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
