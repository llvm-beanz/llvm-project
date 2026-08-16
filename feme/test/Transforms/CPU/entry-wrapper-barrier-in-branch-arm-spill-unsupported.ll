; RUN: not feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; Roadmap step R24's remaining narrowing (see the Status section's
; Deviation note in feme/docs/FeMeCPUDesign.md): a value live across a
; `..._with_group_sync` barrier *within* a branch arm is diagnosed rather
; than silently passed a null spill buffer -- `feme::cpu::EntryWrapperPass`
; allocates only one `barrier_spill` buffer per wrapper today, which a
; branch's two independently-split arms cannot safely share.

; CHECK: error: feme-cpu-wrap-entry: function 'main' has a value live across a group-sync barrier inside a branch arm
define void @main() #0 {
entry:
  %gid = call i32 @llvm.dx.group.id(i32 0)
  %cond = icmp eq i32 %gid, 0
  br i1 %cond, label %a, label %b
a:
  %sum = add i32 %gid, 1
  call void @llvm.dx.group.memory.barrier.with.group.sync()
  %doubled = mul i32 %sum, 2
  br label %exit
b:
  br label %exit
exit:
  ret void
}
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
