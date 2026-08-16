; RUN: not feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s 2>&1 | FileCheck %s

; Roadmap milestone 9's narrowing (see GroupShared.h's doc comment):
; roadmap step R23 closes "an access through a getelementptr" for a
; first-level `getelementptr` (see simdize-groupshared-atomic-array.ll),
; but a *second* `getelementptr` off the first one -- a nested groupshared
; array/struct access -- remains diagnosed rather than canonicalized.

; CHECK: error: feme-cpu-simdize: groupshared global 'shared' feeds a nested getelementptr or another unsupported user
define void @main() #0 {
  %p1 = getelementptr inbounds [2 x [4 x i32]], ptr addrspace(3) @shared, i32 0, i32 0
  %p2 = getelementptr inbounds [4 x i32], ptr addrspace(3) %p1, i32 0, i32 2
  %val = load i32, ptr addrspace(3) %p2
  ret void
}
@shared = internal addrspace(3) global [2 x [4 x i32]] undef
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
