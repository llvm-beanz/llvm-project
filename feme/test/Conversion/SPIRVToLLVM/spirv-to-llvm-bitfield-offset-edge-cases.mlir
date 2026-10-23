// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// ENV6(c)(2)(a): `BitFieldInsertPattern`/`BitFieldSExtractPattern`/
// `BitFieldUExtractPattern` each used `Offset` (or an `Offset`-derived
// amount) as a shift-amount operand completely unclamped. Per the SPIR-V
// spec, `Offset` is "always consumed as an unsigned value" and the op's
// *result* is merely "undefined" whenever `Offset >= Size` -- a spec-legal
// input a real shader may still pass (reduced from the real CTS failure
// `dEQP-VK.graphicsfuzz.cov-bitfieldExtract-undefined`, whose own shader
// computes `bitfieldExtract(a, -1, 1)`: a negative literal `Offset`,
// reinterpreted unsigned, is already `4294967295`, far past `Size == 32`).
// An out-of-range shift amount is outright *poison* per the LLVM LangRef,
// though, not merely the spec's own weaker "unspecified value" -- a
// poisoned branch condition anywhere downstream (even transitively) is
// undefined behavior the optimizer may exploit by deleting everything that
// branch could ever reach, with no diagnostic anywhere in the pipeline
// (this is exactly how `cov-bitfieldExtract-undefined` silently rendered
// solid black instead of failing loudly). Fixed by `clampBitFieldShiftAmount`
// clamping `Offset` (and `BitFieldSExtractPattern`'s own `Offset`-derived
// `AmountToShiftLeft`/`AmountToShiftRight`) to `[0, Size - 1]` via
// `llvm.intr.umin` before it is ever used as the real shift amount.

// CHECK-LABEL: llvm.func @uextract_offset_out_of_range
// CHECK: llvm.intr.umin
// CHECK: llvm.lshr
// CHECK: llvm.and
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @uextract_offset_out_of_range(%base : i32, %count : i32) -> i32 "None" {
    %offset = spirv.Constant -1 : i32
    %r = spirv.BitFieldUExtract %base, %offset, %count : i32, i32, i32
    spirv.ReturnValue %r : i32
  }

  // CHECK-LABEL: llvm.func @insert_offset_out_of_range
  // CHECK: llvm.intr.umin
  // CHECK: llvm.shl
  // CHECK: llvm.or
  // CHECK: llvm.return
  spirv.func @insert_offset_out_of_range(%base : i32, %insert : i32, %count : i32) -> i32 "None" {
    %offset = spirv.Constant -1 : i32
    %r = spirv.BitFieldInsert %base, %insert, %offset, %count : i32, i32, i32
    spirv.ReturnValue %r : i32
  }

  // CHECK-LABEL: llvm.func @sextract_offset_out_of_range
  // CHECK: llvm.intr.umin
  // CHECK: llvm.shl
  // CHECK: llvm.ashr
  // CHECK: llvm.return
  spirv.func @sextract_offset_out_of_range(%base : i32, %count : i32) -> i32 "None" {
    %offset = spirv.Constant -1 : i32
    %r = spirv.BitFieldSExtract %base, %offset, %count : i32, i32, i32
    spirv.ReturnValue %r : i32
  }
}
