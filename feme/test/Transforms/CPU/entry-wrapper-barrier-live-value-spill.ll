; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap step R5 (feme/docs/Roadmap.md): a value computed before a
; `..._with_group_sync` barrier and used after it is spilled to a per-wave
; context array rather than being diagnosed -- see "Values live across a
; barrier" in EntryWrapper.cpp's file comment. Every region gets a trailing
; `barrier_spill` parameter; the defining region stores its value at
; `barrier_spill[wave_index]`, and every later region reloads it from there.

; CHECK: %main.barrier_spill = type { i32 }

; CHECK-LABEL: define internal void @main(
; CHECK-SAME: ptr %barrier_spill)
; CHECK: %[[RELOADINDEX:.*]] = sext i32 %wave_index to i64
; CHECK: %sum.reload.slot = getelementptr [4 x i8], ptr %barrier_spill, i64 %[[RELOADINDEX]]
; CHECK: %sum.reload.val = load i32, ptr %sum.reload.slot
; CHECK: %doubled = mul i32 %sum.reload.val, 2
; CHECK: ret void

; CHECK-LABEL: define internal void @main.region0(
; CHECK-SAME: ptr %barrier_spill)
; CHECK: %[[SPILLINDEX:.*]] = sext i32 %wave_index to i64
; CHECK: %sum.spill.slot = getelementptr [4 x i8], ptr %barrier_spill, i64 %[[SPILLINDEX]]
; CHECK: store i32 %sum, ptr %sum.spill.slot
; CHECK: ret void

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: %barrier.spill = alloca [1 x %main.barrier_spill]
; CHECK: call void @main.region0(
; CHECK-SAME: ptr %barrier.spill)
; CHECK: call void @main(
; CHECK-SAME: ptr %barrier.spill)
define void @main() #0 {
  %gidx = call i32 @llvm.dx.group.id(i32 0)
  %gidy = call i32 @llvm.dx.group.id(i32 1)
  %sum = add i32 %gidx, %gidy
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %doubled = mul i32 %sum, 2
  ret void
}
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
