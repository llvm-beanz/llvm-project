; REQUIRES: directx-registered-target
; RUN: llc %s --filetype=obj -o %t.dxcontainer

; A shader entry point takes no parameters of its own -- its inputs arrive
; through stage-IO or resource accesses -- so every parameter of a widened
; wave body belongs to the `feme::cpu::WaveBodyEnv` ABI `feme-cpu-simdize`
; appends. `@main`'s own `i32 %n` survives DXIL codegen and re-import,
; reaching `feme::cpu::EntryWrapperPass` with no argument for it to supply.
;
; `feme::cpu::runPipeline` must surface that as a hard failure with the
; underlying diagnostic (see feme::cpu::Pipeline.cpp's
; `ErrorDiagnosticGuard`, whose per-pass `runAndCheck` this exercises for
; the wrapping stage) rather than either crashing on the `llvm_unreachable`
; in the wrapper's own wave-body parameter dispatch, or carrying on to emit
; an object file with no `feme_cpu_entry_main` wrapper at all (roadmap
; milestone 7 requires widening to produce one), silently discarding the
; entry point rather than failing. See
; feme/test/Transforms/CPU/entry-wrapper-entry-point-parameter-unsupported.ll
; for the same diagnostic at the pass level.
;
; The loop below is deliberate. This file previously drove the same
; end-to-end "diagnostic surfaced, pipeline fails" contract -- roadmap step
; R2's own P0 item in feme/docs/Roadmap.md's 1.6 -- through
; `feme-cpu-linearize`'s divergent-conditional-`continue` diagnostic. It no
; longer does: `llc`'s own `StructurizeCFG` rewrites this shape on the way
; to DXIL into a `Flow`-block form `feme::cpu::LinearizePass` does handle,
; so nothing is diagnosed here until the entry parameter reaches the
; wrapper. That pass's own narrower raw-IR limit (see the Status section's
; milestone 6 deviation note in feme/docs/FeMeCPUDesign.md) is still covered
; directly by
; feme/test/Transforms/CPU/Linearize/unsupported-loop-internal-branch.ll;
; keeping the loop here keeps the round-trip through it covered too.
; RUN: not feme --target=%feme_host_triple %t.dxcontainer -o %t.o 2>&1 | FileCheck %s

; CHECK: feme-cpu-wrap-entry: function 'main' has an unsupported parameter 'n'
; CHECK: feme-cpu pipeline: a diagnostic was reported while wrapping 'main'


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
