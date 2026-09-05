; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L40: a `for (i...) { if (payload[i] != expected) break; }`
; verification loop -- confirmed via `glslangValidator`/`spirv-dis` against a
; real `dEQP-VK.mesh_shader.ext.misc.payload_read` shader -- whose own
; separate, plain uniform trip-count check (`check`, `i < n`) and genuinely
; divergent break check (`body`, comparing a per-invocation ID) both end up,
; after `UnifyLoopExits`/`StructurizeCFG`, needing to reach the loop's one
; shared exit block: `check`'s own "false" (exhausted) exit arm gets routed
; through a `BreakCriticalEdges` relay straight into the divergent break
; check's own merge block (`Flow`) as just one incoming value of its
; condition `phi` -- a *partial*-constant fusion (mixing one literal-
; constant incoming value with `body`'s own genuinely divergent one),
; distinct from the *all*-constant shape H19k's `foldRedundantFlowBlock`
; targets (see `loop-uniform-check-separate-structurized.ll`) and from the
; shape H19k left this loop's own exit-guard block's `phi` in (see below).
;
; Before this fix, `feme::cpu::LoopLinearizer` diagnosed this as an
; unsupported "internal branch" in `check`, since it saw two `OtherCondBrBlocks`
; entries (`check` and `Flow`) it could not distinguish: `UniformityInfo`
; alone cannot tell them apart here, since `check`'s own induction-variable
; use is transitively fed by `Flow`'s own genuinely-divergent join and so
; is (correctly, if unhelpfully) reported divergent too, once `Flow`'s own
; divergence exists anywhere in the loop at all.
;
; `feme::cpu::peelConstantFlowPredecessors` (added by this fix) instead
; peels `check`'s own constant-selected incoming value off `Flow`'s
; condition `phi` directly -- a *structural*, not a uniformity-based, proof
; that `check`'s own decision is a distinct, already-redundant pass-through
; here -- letting `LoopLinearizer` correctly recognize `Flow` alone as the
; loop's one real divergent exit check and mask it, leaving `check`
; completely untouched.

; CHECK-LABEL: define void @main(
; CHECK: check:
; CHECK-NEXT: %cond = icmp slt i32 %i, %n
; CHECK-NEXT: br i1 %cond, label %body, label %check.Flow_crit_edge
; CHECK: feme.cpu.mask.any
define void @main(i32 %n) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  br label %check
check:
  %cond = icmp slt i32 %i, %n
  br i1 %cond, label %body, label %exit
body:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %divergent = icmp eq i32 %tid, %i
  br i1 %divergent, label %brk, label %merge
brk:
  br label %exit
merge:
  br label %latch
latch:
  %inc = add i32 %i, 1
  br label %header
exit:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="8,1,1" }
