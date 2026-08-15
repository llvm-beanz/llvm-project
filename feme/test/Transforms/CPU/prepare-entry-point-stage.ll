; RUN: feme-opt --llvm -passes=feme-cpu-prepare -feme-cpu-stage=vertex -S %s | FileCheck %s
; RUN: not feme-opt --llvm -passes=feme-cpu-prepare -S %s 2>&1 | FileCheck %s --check-prefix=MISSING

; "Canonicalize entry points" (Phase 1) selects by the `feme.shader.stage`
; enumeration ("Stage identity" in feme/docs/FeMeGraphicsDesign.md), not by a
; string comparison against `"compute"`: entry points of another stage are
; simply not candidates, so a module holding one of each is not ambiguous.

; CHECK: define void @vertex_main()
; CHECK-NOT: @mesh_main
define void @vertex_main() #0 {
  ret void
}

define void @mesh_main() #1 {
  ret void
}

; A stage with no entry point in the module is a diagnosable miss, named by
; the stage that was asked for.
; MISSING: error: feme-cpu-prepare: module has no compute entry point

attributes #0 = { "feme.shader.stage"="vertex" }
attributes #1 = { "feme.shader.stage"="mesh" }
