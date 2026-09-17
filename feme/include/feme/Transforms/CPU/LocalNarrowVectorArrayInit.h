//===- LocalNarrowVectorArrayInit.h - Fix local array init stores -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::LocalNarrowVectorArrayInitPass (roadmap
// H69), which fixes a genuine SPIR-V-to-LLVM conversion bug affecting a
// mesh entry's own `Private`/`Function`-storage local array of a
// non-power-of-2-width fixed vector -- e.g. a GLSL/HLSL `uint3 idx[2]`
// local the shader builds up before writing it out to
// `gl_PrimitiveTriangleIndicesEXT` -- distinct from (though caused by the
// same underlying LLVM `DataLayout` fact as) the *intentional* "tight"
// byte-offset convention `feme::cpu::getPackedMeshElementSize` in
// CanonicalizeStage.cpp documents for a mesh entry's per-vertex/per-
// primitive stage-IO *output* array.
//
// Every constant-indexed access into such an array -- including this
// pass's own target, its "whole array" initializing store -- is converted
// by (feme's override of) MLIR's upstream SPIR-V-to-LLVM `AccessChainOp`
// conversion into a `getelementptr` with a byte offset computed from each
// element's *tightly packed* size (e.g. 12 for a `<3 x i32>`), not real
// LLVM's own ABI-padded `DataLayout::getTypeAllocSize` (16, rounding a
// 3-wide vector up to its own 4-wide SIMD register size). A single
// `store [N x <M x T>] <constant>, ptr @g` of the *whole* array, though,
// is not converted through that same per-element access-chain machinery
// at all -- it is a plain aggregate `llvm::StoreInst` real LLVM lowers
// (and, at this pass's own JIT-executed level, a real
// `TargetLoweringBase`/codegen memcpy-equivalent lays out in memory)
// using its own real, ABI-padded layout. So a local array's *initializing*
// store places its second (and every later) element 16 bytes in, while
// every later *read* of that same element (an ordinary access-chain-
// converted `getelementptr`) still assumes 12 -- the classic "write one
// layout, read a different one" corruption this pass fixes by rewriting
// the aggregate initializing store into one store per array element, each
// addressed at the very same tightly packed byte offset every later
// access-chain-converted read already assumes, so both sides finally
// agree.
//
// Deliberately scoped to a `Private`/`Function`-storage global (address
// space 0, per the storage-class-to-address-space mapping
// `feme::spirv::StageIOGlobalVariablePattern`'s higher-benefit override
// otherwise claims for `Input`/`Output` -- see SPIRVToLLVMPatterns.cpp):
// a mesh entry's own per-vertex/per-primitive stage-IO array never
// receives a single aggregate constant "whole array" store like this to
// begin with (each of its elements is instead written independently,
// per-invocation, by `feme::cpu::MeshOutputWrapperPass`'s own lowering),
// so this pass would be a no-op there even without the address-space
// guard; the guard exists purely as defense in depth, so a future change
// to how stage-IO arrays are initialized can never accidentally feed this
// pass a case `feme::cpu::getPackedMeshElementSize`'s own callers still
// need read as tightly packed.
//
// Roadmap L99: the address-space guard alone is not enough. A
// `spirv.MatrixType`-turned array of columns (a plain, `Private`-storage
// module-scope global, e.g. `mat2x3 m0 = ...`, address space 0 just like
// this pass's own H69 target) shares the exact same
// array-of-narrow-vector shape, but is read back element-wise (`m[i][j]`)
// through MLIR upstream's own generic, natural-ABI-strided `AccessChainOp`
// conversion -- never through the tight, `i8`-offset-GEP convention this
// pass's own H69 fix assumed every reader used. Rewriting *that* global's
// init store into a tight-offset one would therefore introduce the exact
// "write one layout, read a different one" corruption this pass exists to
// prevent, not avoid it. So this pass additionally requires at least one
// of the global's own *other* users to already be a tight (`i8`-element)
// GEP before it rewrites the init store at all -- see `hasTightGEPUser`'s
// own comment in the `.cpp` file.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_LOCALNARROWVECTORARRAYINIT_H
#define FEME_TRANSFORMS_CPU_LOCALNARROWVECTORARRAYINIT_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Rewrites a `Private`/`Function`-storage local array-of-narrow-vector
/// global's single aggregate "whole array" initializing store into one
/// store per array element, each addressed at the same tightly packed
/// byte offset every later access-chain-converted read of that element
/// already assumes. See the file comment above for why this specific
/// mismatch is a genuine memory-corruption bug, not the documented,
/// intentional "tight offset" convention a mesh entry's stage-IO output
/// array relies on.
class LocalNarrowVectorArrayInitPass
    : public llvm::OptionalPassInfoMixin<LocalNarrowVectorArrayInitPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() {
    return "feme-cpu-fix-local-narrow-vector-array-init";
  }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_LOCALNARROWVECTORARRAYINIT_H
