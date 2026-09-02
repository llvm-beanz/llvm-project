; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-push-constants -S %s | FileCheck %s

; Roadmap L10: a push-constant block ending in a non-power-of-two-width
; vector member (`<3 x i32>`, HLSL's own `int3`) gets a real x86-64
; `DataLayout` store size padded past its last real member: the `<3 x i32>`
; member's own natural alignment rounds its 12-byte store size up to the
; next power of two (16), inflating the *whole struct's* trailing padding
; (needed only for a hypothetical array of this struct, which a push
; constant block -- always a single instance -- never has) well past the
; last byte any real, compile-time-constant-indexed load actually reads.
; Here, `@reads_last_real_byte`'s own load of `v3`'s last component sits at
; byte offset 24 (four bytes wide, ending at byte 28) -- but the struct's
; own padded store size is 32. The attached `!feme.cpu.resources`
; metadata's root-constant-size operand (and the runtime bounds check
; above) must report the tighter, genuinely-accessed 28, not the padded
; declared-type size of 32, so that a `VkPushConstantRange` sized to cover
; only what a shader's own loads actually touch (as `offload-test-suite`'s
; own harness always emits) is still accepted rather than spuriously
; rejected for leaving unread tail padding uncovered.
%PaddedPushConstants = type { float, i32, <2 x float>, <3 x i32> }
@pc = external addrspace(13) constant %PaddedPushConstants

; CHECK-LABEL: define i32 @reads_last_real_byte(
; CHECK-SAME: ptr %root_constants, i32 %root_constant_size)
; CHECK: %push_const.inbounds = icmp ule i32 28, %root_constant_size
; CHECK: %push_const.ptr = getelementptr inbounds i8, ptr %root_constants, i64 24
; CHECK: %push_const.load = load i32, ptr %push_const.ptr
define i32 @reads_last_real_byte() {
  %p = getelementptr inbounds %PaddedPushConstants, ptr addrspace(13) @pc, i32 0, i32 3, i32 2
  %v = load i32, ptr addrspace(13) %p
  ret i32 %v
}

; CHECK: !feme.cpu.resources = !{![[MD:[0-9]+]]}
; CHECK: ![[MD]] = !{!"reads_last_real_byte", i32 28, i1 false, i32 0, i32 0, i32 24}
