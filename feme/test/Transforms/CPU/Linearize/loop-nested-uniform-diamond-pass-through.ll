; RUN: feme-opt --llvm -passes=feme-cpu-prepare,feme-cpu-linearize -S %s | FileCheck %s

; Roadmap L42: a `for (i...) { if (j < m) { if (payload[i] != expected)
; break; } }`-shaped verification loop -- confirmed via a real, captured
; pre-`LinearizePass` IR dump of a `dEQP-VK.mesh_shader.ext.misc.
; payload_read` shader -- whose own plain uniform trip-count check
; (`check`, `i < n`) has its "keep looping" arm reach a *second*, still
; entirely uniform, nested check (`check2`, `j < m`) before ever reaching
; the genuinely divergent break check (`body`). `check2`'s own "skip the
; body" arm rejoins the divergent check's own `StructurizeCFG`-built merge
; block directly (`merge`), not the loop's real exit block the way
; `loop-uniform-check-fused-with-divergent-exit.ll`'s single-level nesting
; does -- exercising `feme::cpu::LoopLinearizer::
; collectUniformPassThroughRegion`'s own branching (not merely chained)
; region walk between the loop header and its one real divergent exit
; check, `Flow`, rather than the single-relay-hop shape
; `matchExitCheckWithRelay`'s own `straightChain` alone could tolerate.
;
; Before this fix, `feme::cpu::LoopLinearizer`'s classification loop
; required *every* non-Header/Latch `CondBr` block (both `check` and
; `check2` here, alongside the genuinely divergent `Flow`) to itself pass
; `matchExitCheckWithRelay`, which only `check` (whose own "false" arm
; already reaches the loop's exit block via a single
; `BreakCriticalEdges` relay) could satisfy -- `check2`, whose own "false"
; arm instead rejoins `Flow`'s own merge block via a second, separate
; relay hop, was misdiagnosed as an unsupported internal branch. This fix
; instead classifies `Flow` alone as the loop's one genuine divergent exit
; check (via `UniformityInfo::isDivergentTerminator`, rather than requiring
; every block's own relay match) and validates that both `check` and
; `check2` are covered by a single uniform pass-through *region* between
; the header and `Flow`, rather than a single relayed chain.

; CHECK-LABEL: define void @main(
; CHECK: check:
; CHECK-NEXT: %cond = icmp slt i32 %i, %n
; CHECK: check2:
; CHECK-NEXT: %cond2 = icmp slt i32 %j, %m
; CHECK: feme.cpu.mask.any
define void @main(i32 %n, i32 %m, i32 %j) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%inc, %latch]
  br label %check
check:
  %cond = icmp slt i32 %i, %n
  br i1 %cond, label %check2, label %exit
check2:
  %cond2 = icmp slt i32 %j, %m
  br i1 %cond2, label %body, label %merge
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
