; RUN: feme-opt --llvm -passes=feme-cpu-linearize -S %s | FileCheck %s

; Roadmap ENV6(c)(2)(a): reduced from `dEQP-VK.graphicsfuzz.
; cov-basic-block-discard-in-function`'s own fragment shader -- a helper
; function's `if (x == 10) discard; return x;`, called from a `do`-`while`
; loop whose own uniform trip-count check sits one block *after* the
; reconvergence of that uniform discard-guard diamond, in a block that
; only becomes this cycle's own `Latch` once the pre-existing
; `LatchWasPromoted` fold (see `K6v`) recovers it from behind the trivial
; `crit_edge` relay stub below (`%crit_edge` here mirrors the real case's
; own `"func(i1;.exit._crit_edge"` block exactly).
;
; `Header` (`%header`) is not itself the loop's own exit check -- that is
; `%check`, reached only after the discard diamond (`%discard.block`/
; `%relay`) reconverges -- so this cycle falls through to
; `linearizeCycle`'s general, `!HeaderDivergent`/`!LatchDivergent`
; fallback path with `LatchWasPromoted` true. That path used to apply
; `MasksAtLatch` to every block of `Header`'s own body region with one
; flat, unordered walk, letting `discard.block`'s own unconditional
; `discard` narrow `MasksAtLatch.Live` to a compile-time-constant `false`
; that then applied group-wide regardless of which arm a lane actually
; took -- discarding every pixel this loop ever produced, confirmed via
; `feme-cpu-simdize` producing a hardcoded `and <4 x i1> zeroinitializer,
; ...` loop-continuation mask. Fixed by flattening `Header`'s own
; internal diamond with a genuine per-arm `phi` merge (mirroring
; `DiamondFlattener::flatten`'s own uniform-branch handling) before any
; of `MasksAtLatch` reaches `%check`.
;
; CHECK-LABEL: define void @main(
; CHECK: discard.block:
; CHECK-NOT: call void @feme.stage.discard
; CHECK-NEXT: br label %check
; CHECK: check:
; CHECK-DAG: [[LIVE:%.*]] = phi i1 [ false, %discard.block ], [ %active.live, %relay ]
; CHECK: call i1 @feme.cpu.mask.any(i1 [[LIVE]])
define void @main(i1 %cond) #0 {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%i.next, %crit_edge]
  %discard.cond = icmp eq i32 %i, 10
  br i1 %discard.cond, label %discard.block, label %relay
relay:
  br label %check
discard.block:
  call void @feme.stage.discard(i1 true)
  br label %check
check:
  %i.next = add i32 %i, 1
  %done = icmp sge i32 %i.next, 4
  br i1 %done, label %exit, label %crit_edge
crit_edge:
  br label %header
exit:
  ret void
}
declare void @feme.stage.discard(i1)
attributes #0 = { "feme.shader.stage"="fragment" "hlsl.numthreads"="4,1,1" }
