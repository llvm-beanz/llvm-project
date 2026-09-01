// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L18: `mlir::BranchConditionalConversionPattern` (upstream, `mlir/
// lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`) builds `llvm.cond_br`'s
// successor operands directly from `op.getTrueBlockArguments()`/
// `op.getFalseBlockArguments()` -- the *original*, un-remapped operand
// accessors -- rather than the dialect conversion's own type-converted
// `adaptor.getTrueTargetOperands()`/`adaptor.getFalseTargetOperands()`.
// This is invisible whenever every successor operand's own type converts
// to itself unchanged (e.g. a signless `i32`/`f32`), but an `si32` value
// (HLSL's own `int`, preserved as a distinct SPIR-V dialect type for as
// long as possible, but never a valid LLVM dialect type on its own) merged
// back into a successor block exposes it: the raw op accessor's own value
// still carries the pre-conversion `si32` type, producing the dialect
// conversion legalizer's own "operand #1 must be variadic of LLVM
// dialect-compatible type, but got 'si32'" diagnostic on the freshly built
// `llvm.cond_br` (reduced from the real
// `Feature/StructuredBuffer/packed.test`, whose `if (Fido.TailState == 0)`
// merges an `si32`-typed value back into its join block this same way).
//
// Fixed by `BranchConditionalPattern` (`SPIRVToLLVMPatterns.cpp`),
// registered at `FeMeBenefit` so it wins over the upstream pattern for
// every `spirv.BranchConditional`, using the adaptor's own already
// type-converted successor operands instead.

// CHECK-LABEL: llvm.func @merges_signed_value
// CHECK: llvm.cond_br %{{.*}}, ^[[TRUE:.*]](%{{.*}} : i32), ^[[FALSE:.*]](%{{.*}} : i32)
// CHECK: ^[[TRUE]](%[[TRUEARG:.*]]: i32):
// CHECK: llvm.br ^[[JOIN:.*]](%[[TRUEARG]] : i32)
// CHECK: ^[[FALSE]](%[[FALSEARG:.*]]: i32):
// CHECK: llvm.br ^[[JOIN]](%[[FALSEARG]] : i32)
// CHECK: ^[[JOIN]](%[[JOINARG:.*]]: i32):
// CHECK: llvm.return %[[JOINARG]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @merges_signed_value(%cond : i1, %x : si32, %y : si32) -> si32 "None" {
    spirv.BranchConditional %cond, ^true(%x : si32), ^false(%y : si32)
  ^true(%tx : si32):
    spirv.Branch ^join(%tx : si32)
  ^false(%fy : si32):
    spirv.Branch ^join(%fy : si32)
  ^join(%joined : si32):
    spirv.ReturnValue %joined : si32
  }
}
