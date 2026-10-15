; RUN: split-file %s %t
; RUN: llvm-as < %t/valid.ll | llvm-dis | FileCheck %s
; RUN: not llvm-as %t/mismatched-operand-types.ll -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISMATCH

; `llvm.spv.resource.handleequal` (roadmap K6t, feme/lib/Transforms/CPU/
; SPIRVResourceLowering.cpp and feme/lib/Conversion/SPIRVToLLVM/
; SPIRVToLLVMPatterns.cpp) answers whether two resource-handle values
; (overloaded on any type, matching whatever a given target's resource
; handle is represented as) refer to the same underlying binding. Its
; second operand is constrained (`LLVMMatchType<0>`) to be the exact same
; type as its first, so the two values being compared are always
; comparable -- this file is its own isolated, self-contained test (no
; feme/ dependency), confirming the declaration round-trips correctly and
; that the verifier actually enforces that type-matching constraint.

;--- valid.ll
; CHECK-LABEL: @calls_handleequal_on_two_i32s
; CHECK: call i1 @llvm.spv.resource.handleequal.i32(i32 %a, i32 %b)
define i1 @calls_handleequal_on_two_i32s(i32 %a, i32 %b) {
  %r = call i1 @llvm.spv.resource.handleequal.i32(i32 %a, i32 %b)
  ret i1 %r
}
declare i1 @llvm.spv.resource.handleequal.i32(i32, i32)

;--- mismatched-operand-types.ll
; MISMATCH: intrinsic argument 1 type (matching overload type 0) expected i32, but got ptr
define i1 @invalid_mismatch(i32 %a, ptr %b) {
  %r = call i1 @llvm.spv.resource.handleequal.i32(i32 %a, ptr %b)
  ret i1 %r
}
declare i1 @llvm.spv.resource.handleequal.i32(i32, ptr)
