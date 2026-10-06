; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-push-constants -S %s | FileCheck %s

; Roadmap L386: a `phi` merging two push-constant pointers with different
; compile-time-constant byte offsets, reached along two different
; control-flow paths, before a single shared load -- the shape a branchy
; multisample-resolve fragment shader's own two-different-offset
; resolve-sample logic reduces to (the real
; `dEQP-VK.pipeline.fast_linked_library.multisample.misc.
; input_attachments.render_to.*.ds_resolve_sample_zero.*` failure this
; test is a minimized reproduction of). See `SPIRVPushConstantPhiAccess`'s
; own comment in the header for exactly what is (and is not) recognized.
;
; Before this fix, `visitPushConstantUsers` rejected a `phi` user outright
; (any user that was not itself a load or a further GEP was unsupported),
; which failed `matchSPIRVPushConstantAccess` for the *whole* function --
; leaving it entirely unrewritten, still referencing `@pc` as an external
; declaration with no definition, producing a
; `JIT session error: Symbols not found: [ pc ]` at pipeline-creation time
; rather than a valid (if runtime-bounds-checked) load.
;
; There is no single `BaseOffset` to assign the shared `load` below the
; `phi` -- which offset is correct depends on which edge control flow took
; -- so the fix instead sinks a bounds-checked load to each of the `phi`'s
; own incoming edges (inside `%then`/`%else` below), and merges the two
; resulting *values* with a new `phi` (`push_const.phi_value`) that
; replaces the original *pointer* `phi` outright; the original shared
; `load` becomes dead and is removed entirely.

%PushConstants = type { i32, float, [4 x i32] }
@pc = external addrspace(13) constant %PushConstants

; A `phi` with one incoming edge that does not itself resolve to the
; push-constant global (here, a plain `null`) is conservatively left
; entirely alone -- this pass cannot soundly attribute that edge's own
; value to the push-constant block, so, per the file comment's "leave it
; alone rather than partially lower it" contract, the whole function is
; skipped, exactly as if it had an unsupported plain-GEP user. Printed
; first, in its original module position: unlike a lowered function (see
; `phi_of_differently_offset_geps` below), a function this pass leaves
; unmodified is not moved to the module's end (see
; `SPIRVPushConstantLoweringPass::run`).
; CHECK-LABEL: define i32 @phi_incomplete_edge(
; CHECK-SAME: i1 %cond)
; CHECK-NOT: root_constants
; CHECK: phi ptr addrspace(13)
define i32 @phi_incomplete_edge(i1 %cond) {
entry:
  br i1 %cond, label %then, label %else
then:
  %p.then = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 1
  br label %merge
else:
  br label %merge
merge:
  %p = phi ptr addrspace(13) [ %p.then, %then ], [ null, %else ]
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; A `phi` of push-constant pointers whose own result feeds a further GEP
; (rather than only plain loads) is also left entirely alone -- a further
; GEP or a nested `phi` downstream of a recognized `phi` merge is out of
; this pass's current scope (see the file comment's own scope note).
; CHECK-LABEL: define i32 @phi_feeds_further_gep(
; CHECK-SAME: i1 %cond)
; CHECK-NOT: root_constants
; CHECK: phi ptr addrspace(13)
define i32 @phi_feeds_further_gep(i1 %cond) {
entry:
  br i1 %cond, label %then, label %else
then:
  %p.then = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 1
  br label %merge
else:
  %p.else = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 0
  br label %merge
merge:
  %p = phi ptr addrspace(13) [ %p.then, %then ], [ %p.else, %else ]
  %p2 = getelementptr i32, ptr addrspace(13) %p, i32 1
  %v = load i32, ptr addrspace(13) %p2
  ret i32 %v
}

; The real, supported shape: every incoming edge resolves to the global,
; and the `phi`'s own result is read only by plain loads. A lowered
; function is rebuilt (its signature grows), so it is moved to the
; module's end -- printed last, after both unmodified functions above.
; CHECK-LABEL: define i32 @phi_of_differently_offset_geps(
; CHECK-SAME: i1 %cond, ptr %root_constants, i32 %root_constant_size)
; CHECK-NOT: @pc
;
; CHECK: then:
; CHECK: %push_const.inbounds{{.*}} = icmp ule i32 16, %root_constant_size
; CHECK: %push_const.ptr{{.*}} = getelementptr inbounds i8, ptr %root_constants, i64 12
; CHECK: %push_const.load{{.*}} = load i32, ptr %push_const.ptr{{.*}}, align 4
; CHECK: %[[THEN_VAL:push_const.value[0-9]*]] = phi i32 [ %push_const.load{{.*}}, {{.*}} ], [ 0, {{.*}} ]
;
; CHECK: else:
; CHECK: %push_const.inbounds = icmp ule i32 12, %root_constant_size
; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 8
; CHECK: %push_const.load = load i32, ptr %push_const.ptr, align 4
; CHECK: %[[ELSE_VAL:push_const.value]] = phi i32 [ %push_const.load, {{.*}} ], [ 0, {{.*}} ]
;
; CHECK: %push_const.phi_value = phi i32 [ %[[ELSE_VAL]], {{.*}} ], [ %[[THEN_VAL]], {{.*}} ]
; CHECK-NEXT: ret i32 %push_const.phi_value
define i32 @phi_of_differently_offset_geps(i1 %cond) {
entry:
  br i1 %cond, label %then, label %else
then:
  %p.then = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 1
  br label %merge
else:
  %p.else = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 0
  br label %merge
merge:
  %p = phi ptr addrspace(13) [ %p.then, %then ], [ %p.else, %else ]
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; Regression test for a bug this fix's own first revision shipped (caught
; by a live CTS sweep before landing, not by the three tests above): a
; `phi` edge's own incoming predecessor block (`%then` here) also contains
; an *unrelated* plain push-constant load (`%other.v`) whose own
; bounds-check-and-load codegen is sunk into `%then` by the `Access.Loads`
; rewrite loop -- which runs *before* the `Access.PhiAccesses` loop below
; it -- *before* this `phi` edge's own rewrite ever runs. That earlier
; rewrite's own `SplitBlockAndInsertIfThenElse` call splits `%then`,
; replacing its original `br label %merge` terminator (the one a
; `BasicBlock *` captured once at match time, `SPIRVPushConstantPhiEdge`'s
; original design, would still (wrongly) point at) with a new conditional
; branch; the terminator that actually still reaches `%merge` moves to a
; *new* block. Reading `Phi->getIncomingBlock(Edge.Index)` fresh, right
; before this edge's own rewrite (rather than a once-captured `Pred`), is
; what keeps this edge correct regardless of how many earlier rewrites
; already split its original predecessor block out from under it -- the
; pre-existing `%other.merge` `phi` already in `%merge` (mirroring the
; real CTS failure's own `storemerge1`) is included specifically because
; LLVM's own `splitBasicBlock` keeps *every* `phi` in a split block's
; successors correctly pointed at their real current predecessor, which
; is the exact mechanism this fix now also relies on for its own `phi`.
; CHECK-LABEL: define i32 @phi_edge_shares_block_with_plain_load(
; CHECK-SAME: i1 %cond, ptr %root_constants, i32 %root_constant_size)
;
; `%other.v`'s own bounds-checked rewrite runs first, its own merge-phi
; (for the first, unrelated load) landing in [[OTHER_MERGE:[0-9]+]].
; CHECK: [[OTHER_MERGE:[0-9]+]]: {{.*}}preds = %{{[0-9]+}}, %{{[0-9]+}}
; CHECK-NEXT: %[[OTHER_VAL:push_const.value[0-9]*]] = phi i32
;
; This edge's own rewrite is sunk *into* [[OTHER_MERGE]] (splitting it a
; second time), landing its own merge-phi in [[EDGE_MERGE:[0-9]+]] --
; *not* in the original (now long gone) `%then` block.
; CHECK: [[EDGE_MERGE:[0-9]+]]: {{.*}}preds = %{{[0-9]+}}, %{{[0-9]+}}
; CHECK-NEXT: %[[EDGE_VAL:push_const.value[0-9]*]] = phi i32
; CHECK-NEXT: br label %merge
;
; The pre-existing `%other.merge` phi (mirroring the real CTS failure's
; own `storemerge1`) must reference [[EDGE_MERGE]] -- the real, final
; predecessor after both splits -- not the stale, original `%then`.
; CHECK: %other.merge = phi i32 [ %[[OTHER_VAL]], %[[EDGE_MERGE]] ],
;
; This fix's own new phi must likewise reference [[EDGE_MERGE]], proving
; `Phi->getIncomingBlock(Edge.Index)` was re-read fresh rather than using
; a `BasicBlock *` cached stale at match time.
; CHECK: %push_const.phi_value = phi i32 [ {{.*}}, %[[EDGE_MERGE]] ]
define i32 @phi_edge_shares_block_with_plain_load(i1 %cond) {
entry:
  br i1 %cond, label %then, label %else
then:
  %other = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 0
  %other.v = load i32, ptr addrspace(13) %other
  %p.then = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 1
  br label %merge
else:
  %p.else = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 0
  br label %merge
merge:
  %other.merge = phi i32 [ %other.v, %then ], [ 0, %else ]
  %p = phi ptr addrspace(13) [ %p.then, %then ], [ %p.else, %else ]
  %v = load i32, ptr addrspace(13) %p
  %sum = add i32 %other.merge, %v
  ret i32 %sum
}

; CHECK-DAG: = !{!"phi_of_differently_offset_geps", i32 16, i1 false, i32 0, i32 0, i32 8}
; CHECK-DAG: = !{!"phi_edge_shares_block_with_plain_load", i32 16, i1 false, i32 0, i32 0, i32 0}
