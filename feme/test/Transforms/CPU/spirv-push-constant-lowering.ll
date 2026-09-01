; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-push-constants -S %s | FileCheck %s

; A push-constant block is an ordinary LLVM global in address space 13 (see
; `feme::spirv::PushConstantGlobalVariablePattern`'s header comment in
; SPIRVToLLVMPatterns.cpp), read through plain `getelementptr`+`load`
; instructions rather than a resource-handle intrinsic.

%PushConstants = type { i32, float, [4 x i32] }
@pc = external addrspace(13) constant %PushConstants

; A dynamically-indexed access into the block's array member is left
; entirely alone (see the pass's header comment's scope note), and stays in
; its original module position; `checkSupportedRaisedOps` is left to
; reject whatever remains referencing the global directly.
; CHECK-LABEL: define i32 @dynamic_index(
; CHECK-SAME: i32 %idx)
; CHECK-NOT: root_constants
; CHECK: getelementptr {{.*}} @pc
define i32 @dynamic_index(i32 %idx) {
  %p = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 %idx
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; Each member access becomes a bounds-checked load from the appended
; `root_constants` byte blob, at the member's own constant byte offset. A
; lowered function is rebuilt (its signature grows), so it is moved to the
; module's end -- printed after `@dynamic_index` above, which is left in
; place.
; CHECK-LABEL: define i32 @reads_first_member(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
; CHECK: %push_const.inbounds = icmp ule i32 4, %root_constant_size
; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 0
; CHECK: %push_const.load = load i32, ptr %push_const.ptr
define i32 @reads_first_member() {
  %p = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 0
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; A different member of the same block: a non-zero constant byte offset.
; CHECK-LABEL: define float @reads_second_member(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
; CHECK: %push_const.inbounds = icmp ule i32 8, %root_constant_size
; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 4
; CHECK: %push_const.load = load float, ptr %push_const.ptr
define float @reads_second_member() {
  %p = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 1
  %v = load float, ptr addrspace(13) %p
  ret float %v
}

; A non-zero-offset member access whose `getelementptr` is written directly
; as a constant expression on the `load`'s own pointer operand, rather than
; surviving as a separate `getelementptr` instruction -- the shape LLVM's
; own constant folder collapses this exact access into by default (both the
; global's own address and every index are already compile-time constants),
; and the one a real `dxc`-compiled `offload-test-suite`
; `Feature/PushConstant/bool.test` case's own second (non-zero-offset)
; struct member reduced to. Before this row's own fix, only the zero-offset
; member (needing no `getelementptr` at all, so never hitting this
; constant-expression shape) was recognized, leaving every other member's
; own load unrewritten -- still referencing `@pc` itself, an external
; declaration with no definition -- and producing a JIT symbol-resolution
; failure at run time rather than a compile-time diagnostic.
; CHECK-LABEL: define float @reads_member_via_constant_expr_gep(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
; CHECK: %push_const.inbounds = icmp ule i32 8, %root_constant_size
; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 4
; CHECK: %push_const.load = load float, ptr %push_const.ptr
; CHECK-NOT: @pc
define float @reads_member_via_constant_expr_gep() {
  %v = load float, ptr addrspace(13) getelementptr inbounds (
      %PushConstants, ptr addrspace(13) @pc, i32 0, i32 1)
  ret float %v
}

; Roadmap H7o: the metadata attached to the original function (here a
; stand-in for `!feme.signature`, which a later pass like
; `FragmentWrapperPass` requires to resolve stage-IO element IDs) must
; survive the inline `Function::Create` replacement
; `SPIRVPushConstantLoweringPass::run` performs to append the trailing
; `root_constants`/`root_constant_size` parameters -- this previously
; dropped every function-attached metadata node entirely, since
; `GlobalObject::copyAttributesFrom()` does not copy it. This was the real
; root cause of a genuine `dEQP-VK.pipeline.monolithic.multisample.
; min_sample_shading*` pipeline-creation failure: a push-constant-only
; fragment shader (like the CTS's own `copy_sample_frag`, reading only
; `subpassLoad` and a push constant, no bound descriptor) silently lost
; the `!feme.signature` metadata `feme-cpu-wrap-fragment` requires.
; CHECK-LABEL: define i32 @keeps_metadata(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
; CHECK-SAME: !feme.fake_signature ![[FAKE_MD:[0-9]+]]
define i32 @keeps_metadata() !feme.fake_signature !10 {
  %p = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 0
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}
!10 = !{!"keeps_metadata_marker"}
; CHECK: ![[FAKE_MD]] = !{!"keeps_metadata_marker"}
