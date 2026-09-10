; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L88: an outer *divergent* `if (tid == 0) { <uniform for-loop> }`
; diamond, discovered by a speculative `VK_SUBGROUP_FEATURE_SHUFFLE_BIT`
; flag-flip verification run against `dEQP-VK.subgroups.shuffle.*` (see L85):
; every non-rotate shuffle test's own verification harness wraps its
; `subgroupShuffle`-adjacent check in a uniform loop nested inside the
; test shader's own divergent per-invocation guard, and the very first such
; case aborted the whole `deqp-vk` process with a real
; `llvm::Value::~Value` "Uses remain when a value is destroyed!" assertion.
;
; Root cause: `StructurizeCFG` (run by `feme-cpu-prepare`) shapes the
; uniform loop's own trip-count check the same way
; loop-uniform-check-separate-structurized.ll documents -- a `check` block
; plus a redundant "Flow" merge block re-deriving the same exit decision
; from a phi-of-constants, which `feme::cpu::foldRedundantFlowBlock` (added
; by H19k) knows how to fold away. But because this loop is nested inside
; an *outer divergent* branch here, `feme::cpu::DiamondFlattener` (which
; runs before `LoopLinearizer`/`foldRedundantFlowBlock`) first injects its
; own `live.merge`/`sideeffect.merge` mask `PHINode`s directly into that
; same "Flow" block (the loop's own reconvergence point), then threads
; those mask values all the way up to the *outer* diamond's own,
; far-away reconvergence block, where they are consumed directly by a
; `select` with no phi-chain relationship to "Flow" that
; `foldRedundantFlowBlock`'s narrow phi-forwarding search would ever
; discover. Folding "Flow" away regardless left that outer `select`
; holding a `Use` of a `Value` about to be destroyed.
;
; `foldRedundantFlowBlock` now performs a read-only pre-pass computing
; every phi's real forwarding destination before mutating anything, and
; bails out (leaving the redundant "Flow" block in place, rather than
; risking correctness) the moment it finds a use its own forwarding logic
; cannot account for -- exactly the case here. This is a deliberately
; conservative, safe fallback: nothing this pass produces is required to
; be optimally folded, only correct.

; CHECK-LABEL: define void @main(
; CHECK: Flow2:
; CHECK: select i1 %.linearized, i1 %live.merge{{[0-9]*}}, i1 %live.f{{[0-9]*}}
; CHECK: Flow:
; CHECK: %live.merge{{[0-9]*}} = phi i1
; CHECK: %sideeffect.merge{{[0-9]*}} = phi i1
define void @main(i32 %n, ptr %buf) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c1 = icmp eq i32 %tid, 0
  br i1 %c1, label %outer.t, label %outer.f
outer.t:
  br label %header
header:
  %i = phi i32 [0, %outer.t], [%inc, %latch]
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
  br label %end
outer.f:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
