; RUN: not feme-opt --llvm -passes=feme-cpu-linearize -S %s 2>&1 | FileCheck %s

; Roadmap L383(a) (7th investigation): an outer divergent diamond's own
; true-arm (`t`) is a plain straight-line (`UncondBr`-only) chain that
; happens to pass through the same physical block (`shared`) that the
; false-arm (`y`, itself a nested *uniform* diamond) also branches to
; directly as one of its own two successors -- both arms' own
; `hasNPredecessors(2)` check at their own respective reconvergence point
; passes individually (the outer diamond's own `R` is `end`, which has
; exactly two real predecessors -- `shared` and `y.b` -- and the nested
; diamond's own `R` is also `end`, seeing the exact same two edges), so
; only tracking every block a walk passes through -- not just a divergent
; diamond's own `CondBr`-terminated start block -- catches `shared` being
; reached by the outer arm's own chain *and* by the nested diamond's own
; branch as an unsupported shared-territory collision. Without this,
; `flatten` would later redirect the outer arm's own edge into `shared`
; away to the false-arm, physically stealing one of the nested diamond's
; own two expected predecessors at `end` out from under it, and crash on
; an out-of-range `pred_iterator` dereference. See `feme-cpu-linearize`'s
; own `MergeVisited` comment in feme/lib/Transforms/CPU/Linearize.cpp.

; ENV6(c)(2)(h) update: `validate`'s new predecessor-ownership check
; (see `feme/lib/Transforms/CPU/Linearize.cpp`'s own `ENV6(c)(2)(h)`-
; tagged comment) is a strict generalization of the `MergeVisited`
; mechanism this test originally targeted -- it walks every ordinary
; pass-through block a region's own walk crosses (not just a nested
; `CondBr`'s own reconvergence point) and rejects any of them that is
; also reachable from outside the region, which is exactly this
; shape (`shared` reached both by `t`'s own fallthrough chain and by
; `y`'s own direct branch). It now fires first, during `t`'s own walk,
; before `y`'s nested diamond is ever itself validated, so the specific
; "reached by more than one divergent root" message `MergeVisited` used
; to produce for this case no longer fires -- `MergeVisited` itself is
; unchanged and still guards shapes this newer, more general check does
; not happen to walk through first.
; CHECK: feme-cpu-linearize: function 'main': block 'shared' is reached both by this region's own fallthrough and by a predecessor outside it
define void @main(i32 %uniform_cond) #0 {
entry:
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %c = icmp eq i32 %tid, 0
  br i1 %c, label %t, label %y
t:
  br label %shared
y:
  %uc = icmp sgt i32 %uniform_cond, 0
  br i1 %uc, label %shared, label %y.b
y.b:
  br label %end
shared:
  br label %end
end:
  ret void
}
declare i32 @llvm.dx.thread.id(i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
