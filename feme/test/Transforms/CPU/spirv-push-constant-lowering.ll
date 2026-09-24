; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-push-constants -S %s | FileCheck %s

; A push-constant block is an ordinary LLVM global in address space 13 (see
; `feme::spirv::PushConstantGlobalVariablePattern`'s header comment in
; SPIRVToLLVMPatterns.cpp), read through plain `getelementptr`+`load`
; instructions rather than a resource-handle intrinsic.

%PushConstants = type { i32, float, [4 x i32] }
@pc = external addrspace(13) constant %PushConstants

; Two *independent* dynamic indices in the same access chain (here, a
; further dynamic byte-index chained onto an already-dynamically-indexed
; GEP) remain out of this pass's scope (see the file comment's "at most
; one independent dynamic term" note) -- reasoning about two
; simultaneously-runtime-computed offsets to derive a safe `MaxOffset`
; bound is a meaningfully harder problem this pass does not attempt, and
; no real shader shape needing it has been observed. Left entirely alone,
; exactly like the single-dynamic-index case used to be. Printed first,
; before every lowered function below: a function this pass leaves
; unmodified keeps its original module position, unlike a lowered one
; (its signature grows, so it is rebuilt and moved to the module's end --
; see `SPIRVPushConstantLoweringPass::run`).
; CHECK-LABEL: define i32 @two_dynamic_indices(
; CHECK-SAME: i32 %i, i32 %j)
; CHECK-NOT: root_constants
; CHECK: getelementptr {{.*}} @pc
define i32 @two_dynamic_indices(i32 %i, i32 %j) {
  %base = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 %i
  %p = getelementptr i32, ptr addrspace(13) %base, i32 %j
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; A dynamically-indexed access into the block's array member (roadmap
; L131: a shader's own runtime-computed, "dynamically uniform" index into
; a push-constant array/vector/matrix column, the shape
; `dEQP-VK.pipeline.monolithic.push_constant.graphics_pipeline.
; dynamic_index_vert`'s own `arrType[dynamicIndex]` compiles down to) is
; recognized and lowered exactly like a constant-offset access, except the
; byte offset itself is computed at runtime -- `BaseOffset +
; DynamicIndex * DynamicStride` (see `SPIRVPushConstantLoad`'s own
; comment) -- rather than folded to a single constant. Before this row's
; own fix, this whole function was left entirely unrewritten (still
; referencing `@pc`, an external declaration with no definition),
; producing a `JIT session error: Symbols not found: [ pc ]` at run time
; rather than a valid (if runtime-bounds-checked) load.
; CHECK-LABEL: define i32 @dynamic_index(
; CHECK-SAME: i32 %idx, ptr %root_constants, i32 %root_constant_size)
; CHECK: %[[IDX64:[0-9]+]] = sext i32 %idx to i64
; CHECK: %push_const.dyn_offset = mul i64 %[[IDX64]], 4
; CHECK: %push_const.byte_offset = add i64 %push_const.dyn_offset, 8
; CHECK: %[[END32:[0-9]+]] = trunc i64 %push_const.byte_offset to i32
; CHECK: %push_const.end_offset = add i32 %[[END32]], 4
; CHECK: %push_const.inbounds = icmp ule i32 %push_const.end_offset, %root_constant_size
; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 %push_const.byte_offset
; CHECK: %push_const.load = load i32, ptr %push_const.ptr
define i32 @dynamic_index(i32 %idx) {
  %p = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2, i32 %idx
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; The same dynamically-indexed shape, but based off a nonzero-offset
; struct member reached through a constant-expression `getelementptr`
; (see `reads_member_via_constant_expr_gep` below for why that shape
; arises) rather than directly off `@pc` -- the real shape
; `dynamic_index_vert`'s own `arrType[dynamicIndex]` (the block's own
; last member, at a nonzero byte offset within its own larger, 4-member
; push-constant struct -- this test file's own struct only has 3 members,
; so the exact byte offset differs, but the shape is the same) reduces
; to: an outer, dynamically-indexed `getelementptr` instruction whose own
; base pointer is itself a constant-offset `getelementptr` constant
; expression on `@pc`, not `@pc` directly. Before this row's own fix, even
; a version of this pass already carrying separate (never-landed)
; dynamic-index support for the directly-off-`@pc` shape above would
; still have silently skipped this one entirely: a constant-expression
; GEP's own users were assumed to always be loads (see `git log` on this
; file for the pre-L131 shape of `matchSPIRVPushConstantAccess`), so a
; further, dynamically-indexed GEP based on it was invisibly dropped
; rather than rejected or lowered.
; CHECK-LABEL: define i32 @dynamic_index_nonzero_base(
; CHECK-SAME: i32 %idx, ptr %root_constants, i32 %root_constant_size)
; CHECK: %[[IDX64:[0-9]+]] = sext i32 %idx to i64
; CHECK: %push_const.dyn_offset = mul i64 %[[IDX64]], 4
; CHECK: %push_const.byte_offset = add i64 %push_const.dyn_offset, 8
; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 %push_const.byte_offset
; CHECK: %push_const.load = load i32, ptr %push_const.ptr
define i32 @dynamic_index_nonzero_base(i32 %idx) {
  %p = getelementptr i32, ptr addrspace(13) getelementptr inbounds (
      %PushConstants, ptr addrspace(13) @pc, i32 0, i32 2), i32 %idx
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

; Roadmap H6u: a function whose own push-constant accesses never touch
; the leading bytes of the shared block (the real shape a SPIR-V
; push-constant block declared with a nonzero leading `layout(offset=N)`
; produces -- e.g. `VK_EXT_mesh_shader`'s task stage reading only its own,
; higher-offset portion of a block a mesh stage's own lower-offset portion
; shares) must report a `RootConstantMinOffset` above 0 in its own
; `!feme.cpu.resources` entry, not silently assume every function's own
; accessed span starts at byte 0 -- see `pushConstantsCoverRootConstantSize`
; in Pipeline.cpp, which uses this to scope its own coverage check to only
; the bytes this function's own reflected access span actually reaches.
; CHECK-LABEL: define float @reads_only_second_member(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
define float @reads_only_second_member() {
  %p = getelementptr inbounds %PushConstants, ptr addrspace(13) @pc, i32 0, i32 1
  %v = load float, ptr addrspace(13) %p
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
; CHECK-DAG: = !{!"reads_only_second_member", i32 8, i1 false, i32 0, i32 0, i32 4}
; CHECK-DAG: ![[FAKE_MD]] = !{!"keeps_metadata_marker"}
