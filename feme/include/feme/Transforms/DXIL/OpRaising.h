//===- OpRaising.h - Raise dx.op.* calls to idiomatic LLVM IR ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::dxil::OpRaisingPass, the "op raising" pass called
// for under the DXIL section of feme/docs/Design.md: the semantic inverse of
// LLVM's `DXILOpLowering` pass (llvm/lib/Target/DirectX/DXILOpLowering.cpp),
// rewriting calls to DXIL's `dx.op.*` functions back into the standard LLVM
// IR / `llvm.dx.*` intrinsic calls `DXILOpLowering` lowered them from.
//
// This is deliberately incremental (see the Design.md "Status" note this
// pass's introduction updates): it covers every DXIL opcode with a direct,
// context-free 1:1 mapping back to a single LLVM intrinsic call -- scalar/
// vector math, bit-manipulation, screen-space derivatives, thread/wave/quad
// queries, and so on -- `IsFinite`/`IsNormal` (raised via the generic
// `llvm.is.fpclass` intrinsic, reconstructing an extra constant operand
// rather than a bare 1:1 call), `Barrier` (raised via its constant mode
// operand, selecting one of the six barrier-scope intrinsics -- required
// raised IR for the CPU target, see feme/docs/FeMeCPUDesign.md's "Raised IR
// prerequisites"), and the aggregate-returning ops (`IMul`/`UMul`, `UAddc`,
// `SplitDouble`, `WaveActiveBallot`; raised via a general multi-return-value
// `extractvalue`-reconstruction mechanism). It does not yet cover: ops that
// pick their source intrinsic from an extra "kind"/flag operand rather than
// the opcode alone (`WaveActiveOp`, `WaveActiveBit`, `WavePrefixOp`,
// `QuadOp`), or resource-handle ops (`CreateHandle`, `AnnotateHandle`,
// typed/raw buffer loads and stores, ...), which need `llvm::hlsl`-style
// resource metadata reconstruction -- see OpRaising.cpp for the scope of
// what's covered there specifically. Opcodes not (yet) covered are left as
// unmodified `dx.op.*` calls rather than erroring, so this pass can be used
// incrementally on modules that mix raised and not-yet-raised operations.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_DXIL_OPRAISING_H
#define FEME_TRANSFORMS_DXIL_OPRAISING_H

#include "llvm/IR/PassManager.h"

namespace feme {
namespace dxil {

/// Rewrites calls to `dx.op.*` functions (DXIL's calling-convention encoding
/// of its operations, see `llvm/lib/Target/DirectX/DXIL.td`) back into the
/// `llvm.dx.*`/standard LLVM intrinsic calls they were lowered from by
/// `DXILOpLowering`, for the subset of opcodes this pass currently covers.
class OpRaisingPass : public llvm::PassInfoMixin<OpRaisingPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-dxil-raise-ops"; }
};

} // namespace dxil
} // namespace feme

#endif // FEME_TRANSFORMS_DXIL_OPRAISING_H
