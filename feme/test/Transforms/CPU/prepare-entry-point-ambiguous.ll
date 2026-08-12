; RUN: not feme-opt --llvm -passes=feme-cpu-prepare -S %s 2>&1 | FileCheck %s

; "Canonicalize entry points" (Phase 1) diagnoses an ambiguous module -- more
; than one compute entry point, with no way given to pick between them --
; rather than guessing.

; CHECK: error: feme-cpu-prepare: module has more than one compute entry point
define void @main() #0 {
  ret void
}

define void @other_entry() #0 {
  ret void
}

attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
