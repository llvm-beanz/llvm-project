; RUN: feme-opt --llvm -passes=feme-cpu-lower-resources,feme-cpu-linearize,feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; A `feme.cpu.resource.*` call inside a masked loop (see
; `feme/test/Transforms/CPU/Linearize/loop-resource-mask.ll`) is scalarized
; once widened, exactly like a resource call with any other divergent
; operand: the call's own governing mask (the loop's per-iteration "active"
; mask) is ANDed into the wave's entry mask before extracting each lane's
; value, so a lane the loop already deactivated never touches memory (see
; "masked feme.cpu.resource.* call" in "Phase 4: Widening").

; CHECK-LABEL: define void @main(
; CHECK: latch:
; CHECK: %resource.mask = and <4 x i1> %wave_entry_mask, %active.header.wide
; CHECK-COUNT-4: call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0,
; CHECK-NOT: call void @feme.cpu.resource.store.raw.i32(
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
