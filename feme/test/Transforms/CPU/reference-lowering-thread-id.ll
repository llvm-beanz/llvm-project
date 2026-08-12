; RUN: feme-opt --llvm -passes=feme-cpu-reference-lower-builtins -S %s | FileCheck %s

; `--reference`'s builtin half (feme::cpu::ReferenceLoweringPass, see the
; "CFG restructurization test suite" section of
; feme/docs/FeMeCPUDesign.md): with NumThreads = (4, 1, 1), thread_id.x
; reduces to the flat per-invocation index modulo 4 (trivially the index
; itself here), plus the group id (also read back from a global) times 4 --
; scalar arithmetic over module-level globals instead of
; feme::cpu::WaveLoweringPass's `<W x i32>` one.

; CHECK-LABEL: define void @main(
; CHECK-NOT: llvm.dx.thread.id
; CHECK: %[[FLAT:.*]] = load i32, ptr @feme.cpu.ref.thread_index_in_group
; CHECK: urem i32 %[[FLAT]], 4
; CHECK: load i32, ptr @feme.cpu.ref.group_id
; CHECK: %doubled = mul i32 %tid, 2
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %doubled = mul i32 %tid, 2
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
