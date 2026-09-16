; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap H159(a) (feme/docs/Roadmap.md): a "barriers inside a uniform
; loop" shape whose latch is not a pure scalar recurrence -- its tail
; reads `%gid`, a per-wave value defined in the latch's own pre-barrier
; half, which does not exist in the wrapper's group-wide scalar latch
; block. Rather than declining the shape, the latch is outlined as the
; loop body's own last per-wave region, exactly like the blocks before
; it. The loop's own induction recurrence (`%i.next`) is computed in the
; header instead, so it is still available to the wrapper's scalar loop.

; The latch's pre-barrier half and its (outlined) tail are two regions.
; CHECK-LABEL: define internal void @main.body0(
; CHECK: %gid1 = urem <4 x i32>
; CHECK: store <4 x i32> %gid1, ptr %gid1.spill

; The outlined latch reloads the spilled per-lane value across the
; barrier, and reads the induction through the header-derived splat it
; takes as its own trailing `loopvarN` parameter.
; CHECK-LABEL: define internal void @main.body1(
; CHECK: %gid1.reload.val = load <4 x i32>, ptr %gid1.reload
; CHECK: %use.wide = add <4 x i32> %{{.*}}, %gid1.reload.val
; CHECK: %use2.wide = mul <4 x i32> %use.wide, %loopvar1

; Nothing of the latch is left to clone: the wrapper's own `loop.latch`
; block holds only the backedge branch.
; CHECK-LABEL: define void @feme_cpu_entry_main(
; CHECK: loop.latch:
; CHECK-NEXT: br label %loop.header

define void @main() #0 {
entry:
  br label %header

header:
  %i = phi i32 [ 0, %entry ], [ %i.next, %flow ]
  %i.next = add i32 %i, 1
  %cmp = icmp ult i32 %i, 4
  br i1 %cmp, label %flow, label %after

flow:
  %gid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 0
  %val = load i32, ptr addrspace(3) %ptr
  %use = add i32 %val, %gid
  %use2 = mul i32 %use, %i
  br label %header

after:
  ret void
}

@shared = internal addrspace(3) global [4 x i32] undef

declare i32 @llvm.dx.thread.id.in.group(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
