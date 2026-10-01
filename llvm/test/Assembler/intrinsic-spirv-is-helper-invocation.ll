; RUN: llvm-as < %s | llvm-dis | FileCheck %s

; `llvm.spv.is.helper.invocation` (SPIR-V's `OpIsHelperInvocationEXT`
; read-only query, mirroring the existing
; `llvm.spv.demote.to.helper.invocation` write intrinsic) returns true both
; for an invocation launched as a helper invocation and for one demoted by a
; preceding `llvm.spv.demote.to.helper.invocation` call, so it is
; deliberately given no `IntrNoMem`/speculatable attributes: it must not be
; hoisted above, or CSE'd across, such a call.

; CHECK: declare i1 @llvm.spv.is.helper.invocation()
declare i1 @llvm.spv.is.helper.invocation()

; CHECK-LABEL: @test_is_helper_invocation
define i1 @test_is_helper_invocation() {
  ; CHECK: %1 = call i1 @llvm.spv.is.helper.invocation()
  %1 = call i1 @llvm.spv.is.helper.invocation()
  ret i1 %1
}
