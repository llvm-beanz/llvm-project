; RUN: feme-opt --llvm -passes=feme-cpu-prepare -feme-cpu-entry-point=main -S %s | FileCheck %s

; "Canonicalize entry points" (Phase 1) keeps the named compute entry point
; and removes another one that is unreachable from it.

; CHECK-LABEL: define void @main(
; CHECK-NOT: define {{.*}} @other_entry(
define void @main() #0 {
  ret void
}

define void @other_entry() #0 {
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
