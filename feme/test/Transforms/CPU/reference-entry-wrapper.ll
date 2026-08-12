; RUN: feme-opt --llvm -passes=feme-cpu-reference-lower-builtins,feme-cpu-wrap-reference-entry -S %s | FileCheck %s

; `--reference`'s entry wrapper (feme::cpu::ReferenceEntryWrapperPass, see
; the "CFG restructurization test suite" section of
; feme/docs/FeMeCPUDesign.md) loops over every invocation in the group
; (here NumThreads = (4, 1, 1), so 4 invocations) and calls the unwidened
; body once per invocation, instead of feme::cpu::EntryWrapperPass's wave
; loop over a widened body.

; CHECK: define internal void @main(
; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: invocation.loop.header:
; CHECK: %flat = phi i32 [ 0, %entry ], [ %flat.next, %invocation.loop.body ]
; CHECK: %invocation.cond = icmp ult i32 %flat, 4
; CHECK: invocation.loop.body:
; CHECK: call void @main(
; CHECK: invocation.loop.exit:
; CHECK: ret void
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %doubled = mul i32 %tid, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
