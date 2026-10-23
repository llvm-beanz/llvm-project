// RUN: mlir-opt -convert-spirv-to-llvm %s | FileCheck %s

// Per the SPIR-V spec (`OpShiftRightLogical`/`OpShiftLeftLogical`/
// `OpShiftRightArithmetic`): "Results are undefined if Shift is greater
// than or equal to the bit width of the components of Base" -- a
// spec-legal input a real shader may still pass (reduced from the real
// GraphicsFuzz CTS failure `dEQP-VK.graphicsfuzz.cov-fold-shift-gte32`,
// whose own shader shifts by literal constants 32/33/345 against a 32-bit
// `Base`, deliberately defeating any attempt to constant-fold the shift
// away, while the shader itself never actually depends on the shifted
// value's own bits). `ShiftPattern`'s own conversion used to lower
// `Shift` straight into `llvm.lshr`/`llvm.shl`/`llvm.ashr`'s shift-amount
// operand unclamped: an out-of-range shift amount is outright *poison* per
// the LLVM LangRef, a strictly stronger, UB-carrying property than
// SPIR-V's own "the value is merely unspecified" -- a poison value
// reaching so much as a branch condition anywhere downstream (even
// transitively) makes that branch itself undefined behavior, which the
// optimizer is then entitled to exploit by deleting everything the branch
// could ever reach, with no diagnostic anywhere in the pipeline. Fixed by
// clamping `Shift` to `[0, Size - 1]` (`llvm.intr.umin`) before it is ever
// used as the real shift amount.

// CHECK-LABEL: @shift_right_logical_out_of_range
spirv.func @shift_right_logical_out_of_range(%arg0 : i32) -> i32 "None" {
  // CHECK: %[[SHIFT:.*]] = llvm.mlir.constant(345 : i32) : i32
  %shift = spirv.Constant 345 : i32
  // CHECK: %[[MAX:.*]] = llvm.mlir.constant(31 : i32) : i32
  // CHECK: %[[CLAMPED:.*]] = llvm.intr.umin(%[[SHIFT]], %[[MAX]]) : (i32, i32) -> i32
  // CHECK: llvm.lshr %{{.*}}, %[[CLAMPED]] : i32
  %r = spirv.ShiftRightLogical %arg0, %shift : i32, i32
  spirv.ReturnValue %r : i32
}

// CHECK-LABEL: @shift_left_logical_out_of_range
spirv.func @shift_left_logical_out_of_range(%arg0 : i32) -> i32 "None" {
  // CHECK: %[[SHIFT:.*]] = llvm.mlir.constant(32 : i32) : i32
  %shift = spirv.Constant 32 : i32
  // CHECK: %[[MAX:.*]] = llvm.mlir.constant(31 : i32) : i32
  // CHECK: %[[CLAMPED:.*]] = llvm.intr.umin(%[[SHIFT]], %[[MAX]]) : (i32, i32) -> i32
  // CHECK: llvm.shl %{{.*}}, %[[CLAMPED]] : i32
  %r = spirv.ShiftLeftLogical %arg0, %shift : i32, i32
  spirv.ReturnValue %r : i32
}

// CHECK-LABEL: @shift_right_arithmetic_out_of_range
spirv.func @shift_right_arithmetic_out_of_range(%arg0 : i32) -> i32 "None" {
  // CHECK: %[[SHIFT:.*]] = llvm.mlir.constant(33 : i32) : i32
  %shift = spirv.Constant 33 : i32
  // CHECK: %[[MAX:.*]] = llvm.mlir.constant(31 : i32) : i32
  // CHECK: %[[CLAMPED:.*]] = llvm.intr.umin(%[[SHIFT]], %[[MAX]]) : (i32, i32) -> i32
  // CHECK: llvm.ashr %{{.*}}, %[[CLAMPED]] : i32
  %r = spirv.ShiftRightArithmetic %arg0, %shift : i32, i32
  spirv.ReturnValue %r : i32
}
