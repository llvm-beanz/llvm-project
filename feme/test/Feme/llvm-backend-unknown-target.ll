; RUN: not feme-translate --llvm-backend --target-triple=not-a-real-target-triple %s 2>&1 | FileCheck %s

; feme::TargetMachineBackend (via feme-translate's `--llvm-backend`, see
; feme/docs/Design.md's "Testing Tools" section) must reject a
; --target-triple naming no LLVM target, rather than crashing, per
; "Diagnostics and Error Handling" in feme/docs/Design.md.

define void @foo() {
  ret void
}

; CHECK: no registered LLVM target for triple 'not-a-real-target-triple'
