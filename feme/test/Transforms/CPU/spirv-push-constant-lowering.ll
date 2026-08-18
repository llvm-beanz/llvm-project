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
