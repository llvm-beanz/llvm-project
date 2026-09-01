; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer

; A loop with a divergent conditional `continue` (see
; feme/test/Transforms/CPU/Linearize/unsupported-loop-internal-branch.ll):
; `feme::cpu::LinearizePass` only recognizes a divergent exit check directly
; in a loop's header and/or its latch (see the Status section's milestone 6
; deviation note in feme/docs/FeMeCPUDesign.md), so it diagnoses this shape
; and leaves it completely untouched, still a genuinely divergent branch.
;
; Before this milestone, `feme::cpu::runPipeline` did not check for that
; diagnostic between passes (a `ModulePassManager::run` has no `Error` to
; propagate one through -- see `feme::cpu::checkSupportedRaisedOps`'s own
; comment for the same constraint), so the pipeline carried on: the
; unwidened, still-divergent function reached the JIT (`feme-run`) or the
; retargeted object file (`feme`, tested here) despite `feme-cpu-linearize`
; already having said it could not handle it -- an object file with no
; `feme_cpu_entry_main` wrapper (roadmap milestone 7 requires widening to
; produce one), silently discarding the entry point rather than failing.
; This is now a hard failure with the underlying diagnostic surfaced (see
; feme::cpu::Pipeline.cpp's `ErrorDiagnosticGuard`), matching the "divergent
; branch inside a loop" P0 item roadmap step R2 closes in
; feme/docs/Roadmap.md's §1.6.
; RUN: not feme --target=%feme_host_triple %t.dxcontainer -o %t.o 2>&1 | FileCheck %s

; CHECK: feme-cpu-linearize: function 'main': loop at 'loop' has an internal branch in
; CHECK: feme-cpu pipeline: a diagnostic was reported while linearizing 'main'

target triple = "dxil-unknown-shadermodel6.5-compute"

define void @main(i32 %n) #0 {
entry:
  br label %loop
loop:
  %i = phi i32 [0, %entry], [%inc, %latch]
  %loop.cond = icmp slt i32 %i, %n
  br i1 %loop.cond, label %body, label %exit
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %continue.cond = icmp eq i32 %tid, %i
  br i1 %continue.cond, label %latch, label %work
work:
  br label %latch
latch:
  %inc = add i32 %i, 1
  br label %loop
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)

attributes #0 = { "hlsl.numthreads"="4,1,1" "hlsl.shader"="compute" }

!dx.valver = !{!0}
!0 = !{i32 1, i32 8}
