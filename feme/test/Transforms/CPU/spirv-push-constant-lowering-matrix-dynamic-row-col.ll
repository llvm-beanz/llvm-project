; RUN: feme-opt --llvm -passes=feme-cpu-lower-spirv-push-constants -S %s | FileCheck %s

; Roadmap ENV6(c)(2)(c): reduced from `dEQP-VK.graphicsfuzz.
; cov-read-matrix-push-constant`'s own `FEME_DUMP_IR_PRENORM=1` dump --
; a push-constant `mat4` read dynamically indexed on *both* its row and
; its column (`matrix[dynamicRow][dynamicCol]`), using byte-granular GEPs
; (the shape `feme-translate`'s SPIR-V import actually produces for a
; dynamically-indexed matrix member, rather than a typed
; `getelementptr <4 x float>, ...` GEP). Before this fix, any second
; independent dynamic term anywhere in a push-constant access chain left
; the whole function -- and so the push-constant global itself --
; entirely unconverted, surfacing only as a
; `JIT session error: Symbols not found: [ spirv_var_11 ]` at run time.
;
; CHECK-LABEL: define float @read_matrix_dynamic_row_col(
; CHECK-SAME: i32 %row, i32 %col, ptr %root_constants, i32 %root_constant_size)
; CHECK: %push_const.dyn_term = mul i64 %row64, 16
; CHECK: %push_const.dyn_term1 = mul i64 %col64, 4
; CHECK: %push_const.dyn_combined = add i64 %push_const.dyn_term, %push_const.dyn_term1
; CHECK: %push_const.dyn_offset = mul i64 %push_const.dyn_combined, 1
; CHECK: %push_const.byte_offset = add i64 %push_const.dyn_offset, 0
%PushConstants = type <{ [4 x <4 x float>] }>
@spirv_var_11 = external addrspace(13) constant %PushConstants

define float @read_matrix_dynamic_row_col(i32 %row, i32 %col) {
  %row64 = sext i32 %row to i64
  %col64 = sext i32 %col to i64
  %rowptr = getelementptr [16 x i8], ptr addrspace(13) @spirv_var_11, i64 %row64
  %elemptr = getelementptr [4 x i8], ptr addrspace(13) %rowptr, i64 %col64
  %v = load float, ptr addrspace(13) %elemptr
  ret float %v
}
