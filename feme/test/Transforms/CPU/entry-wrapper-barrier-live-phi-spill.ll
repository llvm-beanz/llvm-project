; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step R24 (feme/docs/Roadmap.md): a `phi` live across a
; `..._with_group_sync` barrier is spilled exactly like any other value --
; see "A `phi` live across a barrier" in EntryWrapper.cpp's file comment --
; rather than being diagnosed. The spill store goes after the block's last
; phi (there is only one here) instead of immediately after the phi itself.

; CHECK: %main.barrier_spill = type { <4 x i32> }

; CHECK-LABEL: define internal void @main(
; CHECK-SAME: ptr %barrier_spill)
; CHECK: %val.wide.reload.val = load <4 x i32>, ptr %val.wide.reload
; CHECK: ret void

; CHECK-LABEL: define internal void @main.region0(
; CHECK-SAME: ptr %barrier_spill)
; CHECK: %val.wide = phi <4 x i32>
; CHECK-NEXT: %val.wide.spill.slot = getelementptr
; CHECK-NEXT: %val.wide.spill = getelementptr
; CHECK-NEXT: store <4 x i32> %val.wide, ptr %val.wide.spill
; CHECK: ret void
define void @main() #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  br label %next
next:
  %val = phi i32 [ %tid, %entry ]
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %doubled = mul i32 %val, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
