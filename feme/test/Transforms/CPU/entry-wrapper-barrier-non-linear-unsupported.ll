; RUN: not feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; Roadmap milestone 9's narrowing (see the Status section's Deviation note
; in feme/docs/FeMeCPUDesign.md): a `..._with_group_sync` barrier inside a
; uniform (group-id-derived, so not rejected by `feme::cpu::SIMDizePass`
; itself) but still branchy wave body is diagnosed rather than
; mis-compiled -- `feme::cpu::EntryWrapperPass`'s region splitting only
; supports a straight-line wave body for now.

; CHECK: error: feme-cpu-wrap-entry: function 'main' has a barrier inside non-linear control flow
define void @main() #0 {
entry:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %cond = icmp eq i32 %gid, 0
  br i1 %cond, label %a, label %b
a:
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  br label %exit
b:
  br label %exit
exit:
  ret void
}
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
