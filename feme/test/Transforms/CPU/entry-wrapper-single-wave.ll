; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Phase 6 (feme::cpu::EntryWrapperPass) wraps the widened, wave-lowered
; body in the wave loop and produces the exported
; `feme_cpu_entry_<name>(ptr)` ABI symbol (see "Kernel ABI" in
; feme/docs/FeMeCPUDesign.md): with NumThreads = (4, 1, 1) and W = 4, one
; group is exactly one wave, so the loop runs exactly once.

; CHECK: define internal void @main(
; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: wave.loop.header:
; CHECK: %w = phi i32 [ 0, %entry ], [ %w.next, %wave.loop.body ]
; CHECK: %wave.cond = icmp ult i32 %w, 1
; CHECK: wave.loop.body:
; CHECK: call void @main(
; CHECK: wave.loop.exit:
; CHECK: ret void
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %doubled = mul i32 %tid, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
