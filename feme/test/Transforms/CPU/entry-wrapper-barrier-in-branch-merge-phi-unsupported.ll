; RUN: not feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; Roadmap step R24's remaining narrowing (see the Status section's
; Deviation note in feme/docs/FeMeCPUDesign.md): a branch merge block with
; a phi -- a value one arm computes differently from the other, needed
; after the branch -- is diagnosed rather than mis-compiled.
; `feme::cpu::matchBranchShape` declines this shape (a merge phi means
; threading a value across the wrapper's own scalar branch choice, not
; just across a barrier within one region), so region splitting falls
; back to the straight-line path, which diagnoses the branch itself.

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
  %val = phi i32 [ 1, %a ], [ 2, %b ]
  %doubled = mul i32 %val, 2
  ret void
}
declare i32 @llvm.dx.group.id(i32)
declare void @llvm.dx.group.memory.barrier.with.group.sync()
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
