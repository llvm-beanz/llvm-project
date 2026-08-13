; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; Roadmap milestone 9's narrowing (see GroupShared.h's doc comment and the
; Status section's Deviation note in feme/docs/FeMeCPUDesign.md): a
; divergent (per-lane-varying) groupshared index -- the common
; `groupshared[threadIdInGroup]` pattern -- scalarizes into one
; `getelementptr` clone per lane (`feme::cpu::FunctionWidener`'s
; scalarization fallback), each feeding an `insertelement` rather than a
; direct `load`/`store`; `feme::cpu::rewriteGroupSharedGlobals` diagnoses
; this shape instead of leaving it unrewired.

; CHECK: error: feme-cpu-simdize: groupshared global 'shared' feeds a nested getelementptr or another unsupported user
define void @main() #0 {
  %tid = call i32 @llvm.dx.thread.id.in.group(i32 0)
  %ptr = getelementptr inbounds [4 x i32], ptr addrspace(3) @shared, i32 0, i32 %tid
  %val = load i32, ptr addrspace(3) %ptr
  %doubled = mul i32 %val, 2
  ret void
}
@shared = internal addrspace(3) global [4 x i32] undef
declare i32 @llvm.dx.thread.id.in.group(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
