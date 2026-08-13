; RUN: not feme-opt --llvm -passes=feme-cpu-simdize,feme-cpu-lower-wave,feme-cpu-wrap-entry -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; Roadmap milestone 9's narrowing (see the Status section's Deviation note
; in feme/docs/FeMeCPUDesign.md): a value computed before a
; `..._with_group_sync` barrier and used after it would need spilling to a
; per-wave context allocation (see "Barriers" in "Phase 6: Group Execution
; and Barriers"), which this milestone does not yet implement -- diagnosed
; rather than silently miscompiled. Groupshared memory is unaffected: it
; already carries state across a barrier correctly (see
; entry-wrapper-barrier-region-split.ll).

; CHECK: error: feme-cpu-wrap-entry: function 'main' has a value live across a group-sync barrier
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
