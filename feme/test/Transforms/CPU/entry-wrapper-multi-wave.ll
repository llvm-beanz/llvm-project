; RUN: feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s | FileCheck %s

; With NumThreads = (8, 1, 1) and W = 4, a group needs two waves, so the
; wave loop runs twice with an all-active mask both times (8 is an exact
; multiple of 4).

; CHECK-LABEL: define void @feme_cpu_entry_main(ptr %args) {
; CHECK: %wave.cond = icmp ult i32 %w, 2
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %doubled = mul i32 %tid, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
