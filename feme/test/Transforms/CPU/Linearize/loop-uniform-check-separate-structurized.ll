; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; A plain, uniform `for (init; cond; ++i) { body }` loop -- `cond` and `++i`
; naturally land in separate blocks (`check`, `latch`), distinct from the
; header -- run through the full `feme-cpu-prepare` pipeline first, so
; `StructurizeCFG` restructures it the way it restructures every such loop
; in practice. Before roadmap H19k, `StructurizeCFG` routed `check`'s own
; exit decision through a "Flow" merge block that re-derived the identical
; decision from a phi selecting between two compile-time constants (one per
; predecessor), splitting what `feme::cpu::LoopLinearizer` needs to see as
; a single exit-check block into two -- diagnosed as an unsupported
; "internal branch", newly reachable for the first time by a real
; `dEQP-VK.image.load_store_multisample.2d.*` verification shader's own
; `for (sampleNdx...)` loop (see roadmap H19g/H19k in feme/docs/Roadmap.md).
;
; `feme::cpu::foldRedundantFlowBlock` (added to
; feme/lib/Transforms/CPU/Linearize.cpp by H19k, run at the top of
; `LoopLinearizer::linearizeCycle`) folds that redundant re-derivation away
; directly -- narrowly, unlike a general CFG-simplification pass: it only
; ever removes a block whose own branch condition is a `phi` every one of
; whose incoming values is a literal compile-time constant, so it can never
; mistake a genuine divergent decision for a redundant one. This loop's own
; check is not divergent, though (`%n` is a uniform argument), so
; `LoopLinearizer` still leaves the loop's real control flow alone -- no
; `feme.cpu.mask` machinery appears -- but the redundant "Flow" merge block
; (and the critical-edge relay `BreakCriticalEdges` built around it) is
; gone regardless, a side effect of folding it unconditionally rather than
; only once a check turns out divergent.

; CHECK-LABEL: define void @main(
; CHECK-NOT: feme.cpu.mask
; CHECK: icmp slt i32 %i, %n
; CHECK: br i1 %{{.*}}, label %body, label %check.Flow_crit_edge
define void @main(i32 %n, ptr %buf) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  br label %check
check:
  %cond = icmp slt i32 %i, %n
  br i1 %cond, label %body, label %exit
body:
  store i32 %i, ptr %buf
  br label %latch
latch:
  %inc = add i32 %i, 1
  br label %header
exit:
  ret void
}
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
