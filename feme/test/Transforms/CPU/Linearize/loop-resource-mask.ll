; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-linearize -S %s | FileCheck %s

; A `feme.cpu.resource.*` call inside a masked loop (see loop-break.ll's
; header/latch shape) gets its mask operand rewritten to the loop's
; per-iteration "active" mask, the same as a resource call inside a
; divergent diamond's arm (see "Canonical resource calls are similarly
; rewritten to masked forms" in "Phase 3: Linearization and Predication"):
; a resource access on behalf of a lane the header's divergent check already
; deactivated must not touch memory for the rest of that iteration.

; CHECK-LABEL: define void @main(
; CHECK: loop:
; CHECK: %active.header = and i1 %active, %{{.*}}
; CHECK-NEXT: br label %latch
; CHECK: latch:
; CHECK: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %{{.*}}, i32 %i, i1 %active.header)
define void @main(i32 %n) #0 {
entry:
  %h = call target("dx.RawBuffer", i32, 1, 0) @llvm.dx.resource.handlefromheap(i32 0, i1 false)
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %break.cond = icmp eq i32 %tid, %i
  br i1 %break.cond, label %exit, label %latch
latch:
  call void @llvm.dx.resource.store.rawbuffer.i32(
      target("dx.RawBuffer", i32, 1, 0) %h, i32 %i, i32 0, i32 %i)
  %inc = add i32 %i, 1
  %loop.cond = icmp slt i32 %inc, %n
  br i1 %loop.cond, label %loop, label %exit
exit:
  ret void
}
declare target("dx.RawBuffer", i32, 1, 0) @llvm.dx.resource.handlefromheap(i32, i1)
declare void @llvm.dx.resource.store.rawbuffer.i32(target("dx.RawBuffer", i32, 1, 0), i32, i32, i32)
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
