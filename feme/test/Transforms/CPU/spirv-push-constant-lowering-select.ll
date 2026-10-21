; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-push-constants -S %s | FileCheck %s

; Roadmap H9: a `select` merging two push-constant pointers with different
; compile-time-constant byte offsets before a single shared load -- the
; same shape `spirv-push-constant-lowering-phi.ll` already covers for a
; `phi`, but one step further folded by LLVM's own `InstCombinePass`
; (which the upstream `feme::graphics::
; UnrollConstantTripCountStageLoopsPass` runs over the raw SPIR-V-imported
; module before this pipeline ever sees it): a simple, side-effect-free
; two-predecessor `phi` merge collapses into a single-block `select`
; whenever nothing else in either predecessor block needs to stay split.
; This is a minimized reproduction of the real
; `dEQP-VK.pipeline.*.multisample.misc.multi_renderpass.*` failure, whose
; actual fragment shader computes one of two sample-mask byte offsets
; along two control-flow paths that this exact fold then merges into one
; `select` before its own shared `load`.
;
; Before this fix, `visitPushConstantUsers` rejected a `select` user
; outright (like a `phi`, before `spirv-push-constant-lowering-phi.ll`'s
; own fix) -- any user that was not itself a load or a further GEP was
; unsupported, which failed `matchSPIRVPushConstantAccess` for the *whole*
; function, leaving it entirely unrewritten, still referencing `@pc` as an
; external declaration with no definition, producing a `JIT session
; error: Symbols not found: [ pc ]` at pipeline-creation time.
;
; Unlike a `phi` edge, a `select`'s two arms share the same control-flow
; point as the `select` itself -- there is no separate predecessor block
; to sink each arm's own bounds-checked load into -- so the fix instead
; emits both arms' bounds-checked loads eagerly, right before the
; `select`, and merges the two resulting *values* with a new value-level
; `select` (`push_const.select_value`) sharing the original `select`'s own
; condition.

%PushConstants = type { i32, float, [4 x i32] }
@pc = external addrspace(13) constant %PushConstants

; A `select` with one arm that does not itself resolve to the
; push-constant global (here, a plain `null`) is conservatively left
; entirely alone, per the file comment's "leave it alone rather than
; partially lower it" contract.
; CHECK-LABEL: define i32 @select_incomplete_arm(
; CHECK-SAME: i1 %cond)
; CHECK-NOT: root_constants
; CHECK: select i1 %cond, ptr addrspace(13)
define i32 @select_incomplete_arm(i1 %cond) {
entry:
  %p.then = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 1
  %p = select i1 %cond, ptr addrspace(13) %p.then, ptr addrspace(13) null
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; A `select` of push-constant pointers whose own result feeds a further
; GEP (rather than only plain loads) is also left entirely alone -- out of
; this pass's current scope, exactly like the equivalent `phi` case.
; CHECK-LABEL: define i32 @select_feeds_further_gep(
; CHECK-SAME: i1 %cond)
; CHECK-NOT: root_constants
; CHECK: select i1 %cond, ptr addrspace(13)
define i32 @select_feeds_further_gep(i1 %cond) {
entry:
  %p.then = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 1
  %p.else = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 0
  %p = select i1 %cond, ptr addrspace(13) %p.then, ptr addrspace(13) %p.else
  %p2 = getelementptr i32, ptr addrspace(13) %p, i32 1
  %v = load i32, ptr addrspace(13) %p2
  ret i32 %v
}

; The real, supported shape: both arms resolve to the global, and the
; `select`'s own result is read only by plain loads. A lowered function is
; rebuilt (its signature grows), so it is moved to the module's end --
; printed last, after both unmodified functions above.
; CHECK-LABEL: define i32 @select_of_differently_offset_geps(
; CHECK-SAME: i1 %cond, ptr %root_constants, i32 %root_constant_size)
; CHECK-NOT: @pc
;
; The `false`-arm load (offset 8/size 4, the first operand visited) is
; emitted first.
; CHECK: %push_const.inbounds = icmp ule i32 12, %root_constant_size
; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 8
; CHECK: %push_const.load = load i32, ptr %push_const.ptr, align 4
; CHECK: %[[FALSE_VAL:push_const.value]] = phi i32 [ %push_const.load, {{.*}} ], [ 0, {{.*}} ]
;
; The `true`-arm load (offset 12/size 4) follows.
; CHECK: %push_const.inbounds{{.*}} = icmp ule i32 16, %root_constant_size
; CHECK: %push_const.ptr{{.*}} = getelementptr inbounds i8, ptr %root_constants, i64 12
; CHECK: %push_const.load{{.*}} = load i32, ptr %push_const.ptr{{.*}}, align 4
; CHECK: %[[TRUE_VAL:push_const.value[0-9]*]] = phi i32 [ %push_const.load{{.*}}, {{.*}} ], [ 0, {{.*}} ]
;
; CHECK: %push_const.select_value = select i1 %cond, i32 %[[TRUE_VAL]], i32 %[[FALSE_VAL]]
; CHECK-NEXT: ret i32 %push_const.select_value
define i32 @select_of_differently_offset_geps(i1 %cond) {
entry:
  %p.then = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 1
  %p.else = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 0
  %p = select i1 %cond, ptr addrspace(13) %p.then, ptr addrspace(13) %p.else
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; CHECK-DAG: = !{!"select_of_differently_offset_geps", i32 16, i1 false, i32 0, i32 0, i32 8}
