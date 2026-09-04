// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap H10h: upstream's `BitFieldInsertPattern`/`BitFieldSExtractPattern`/
// `BitFieldUExtractPattern` (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`)
// build their `llvm.shl`/`llvm.lshr`/`llvm.ashr`/`llvm.and`/`llvm.xor` ops
// from a mix of the op's own raw `getBase()`/`getInsert()` accessors and
// `processCountOrOffset(op.getOffset()/getCount(), ...)`, which itself takes
// the raw accessors too -- the same "raw accessor instead of the adaptor's
// type-converted one" bug class as
// spirv-to-llvm-branch-conditional-signed-argument.mlir's
// `BranchConditionalConversionPattern`, just in a different pattern family.
//
// `processCountOrOffset`'s own broadcast/truncate-or-extend logic is a
// no-op whenever `Offset`/`Count`'s width already matches `Base`'s --
// which every real SPIR-V `BitField*` op satisfies, since the spec requires
// same-width operands -- so the raw, pre-conversion `si32`/`ui32`-typed
// value passes straight through into the freshly built `llvm.shl`/
// `llvm.lshr`, producing the dialect conversion legalizer's own "operand #1
// must be ... signless integer ..., but got 'si32'" diagnostic. Reduced
// from the real `dEQP-VK.wsi.xcb.incremental_present.scale_none.fifo.
// identity.opaque.reference` case, whose fragment shader's
// `bitfieldExtract(x, 0, 1)` calls on `highp uint` values lower to
// `spirv.BitFieldUExtract` with `ui32`-typed operands.
//
// Fixed by `BitFieldInsertPattern`/`BitFieldSExtractPattern`/
// `BitFieldUExtractPattern` (`SPIRVToLLVMPatterns.cpp`), registered at
// `FeMeBenefit` so they win over the upstream patterns, using the adaptor's
// own already type-converted `Base`/`Insert`/`Offset`/`Count` throughout.

// CHECK-LABEL: llvm.func @uextract_signed
// CHECK-NOT: si32
// CHECK: llvm.shl
// CHECK: llvm.xor
// CHECK: llvm.lshr
// CHECK: llvm.and
// CHECK: llvm.return

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @uextract_signed(%base : si32, %offset : si32, %count : si32) -> si32 "None" {
    %r = spirv.BitFieldUExtract %base, %offset, %count : si32, si32, si32
    spirv.ReturnValue %r : si32
  }

  // CHECK-LABEL: llvm.func @sextract_signed
  // CHECK-NOT: si32
  // CHECK: llvm.shl
  // CHECK: llvm.ashr
  // CHECK: llvm.return
  spirv.func @sextract_signed(%base : si32, %offset : si32, %count : si32) -> si32 "None" {
    %r = spirv.BitFieldSExtract %base, %offset, %count : si32, si32, si32
    spirv.ReturnValue %r : si32
  }

  // CHECK-LABEL: llvm.func @insert_signed
  // CHECK-NOT: si32
  // CHECK: llvm.shl
  // CHECK: llvm.xor
  // CHECK: llvm.and
  // CHECK: llvm.or
  // CHECK: llvm.return
  spirv.func @insert_signed(%base : si32, %insert : si32, %offset : si32, %count : si32) -> si32 "None" {
    %r = spirv.BitFieldInsert %base, %insert, %offset, %count : si32, si32, si32
    spirv.ReturnValue %r : si32
  }

  // A vector `Base` with scalar `Offset`/`Count`: exercises
  // `processBitFieldCountOrOffset`'s broadcast step (not just the
  // truncate-or-extend one the scalar cases above exercise).
  // CHECK-LABEL: llvm.func @uextract_unsigned_vector
  // CHECK-NOT: ui32
  // CHECK: llvm.insertelement
  // CHECK: llvm.shl
  // CHECK: llvm.lshr
  // CHECK: llvm.and
  // CHECK: llvm.return
  spirv.func @uextract_unsigned_vector(%base : vector<4xui32>, %offset : ui32, %count : ui32) -> vector<4xui32> "None" {
    %r = spirv.BitFieldUExtract %base, %offset, %count : vector<4xui32>, ui32, ui32
    spirv.ReturnValue %r : vector<4xui32>
  }
}
