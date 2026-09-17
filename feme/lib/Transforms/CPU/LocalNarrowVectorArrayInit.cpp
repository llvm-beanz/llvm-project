//===- LocalNarrowVectorArrayInit.cpp - Fix local array init stores -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/LocalNarrowVectorArrayInit.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// The tightly packed (no inter-element padding) size, in bytes, of \p Ty
/// -- the same convention `feme::cpu::getPackedMeshElementSize` in
/// CanonicalizeStage.cpp documents every constant-indexed access-chain-
/// converted `getelementptr` into a mesh entry's own array-of-vector
/// (whether that array is a stage-IO output array or, as here, a genuinely
/// memory-backed local scratch array) actually bakes into its own byte
/// offset, rather than `DataLayout::getTypeAllocSize`'s ABI-alignment-
/// padded size (e.g. a `<3 x i32>` allocates to 16 bytes -- LLVM pads a
/// 3-wide vector up to its own 4-wide SIMD register size -- but is
/// addressed only 12 bytes apart from its neighbor in an array).
uint64_t getTightSize(Type *Ty, const DataLayout &DL) {
  if (auto *ArrTy = dyn_cast<ArrayType>(Ty))
    return ArrTy->getNumElements() *
           getTightSize(ArrTy->getElementType(), DL);
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty))
    return VecTy->getNumElements() *
           getTightSize(VecTy->getElementType(), DL);
  return DL.getTypeAllocSize(Ty);
}

/// Whether \p Ty is an array of a fixed vector whose tightly packed size
/// (see `getTightSize` above) genuinely disagrees with its real,
/// ABI-padded `DataLayout::getTypeAllocSize` -- i.e. whether a plain
/// aggregate store of \p Ty and a later access-chain-converted
/// `getelementptr` into one of its elements would actually disagree on
/// where that element lives. A global whose array element type has no
/// such gap (a scalar, or a full-width vector already a power of 2 wide)
/// needs no fixup at all: an ordinary aggregate store already agrees with
/// every tightly packed offset into it, since the two coincide.
bool hasNarrowVectorArrayPaddingGap(ArrayType *Ty, const DataLayout &DL) {
  auto *VecTy = dyn_cast<FixedVectorType>(Ty->getElementType());
  if (!VecTy)
    return false;
  return getTightSize(VecTy, DL) != DL.getTypeAllocSize(VecTy);
}

/// Whether \p GV has at least one *other* user (besides its own aggregate
/// init store, which this pass is about to rewrite) that already reads
/// one of its elements through a tightly packed byte offset -- i.e. a
/// `getelementptr i8, ptr @GV, i64 <N>` rather than an ordinary,
/// natural-ABI-strided `getelementptr [inner array type], ptr @GV, ...`.
/// H69's own local mesh-scratch-array scenario (`uint3 idx[2]`, copied
/// element-by-element into `gl_PrimitiveTriangleIndicesEXT` by
/// `feme::cpu::MeshOutputWrapperPass`, which computes both that copy's
/// source and destination addresses using the very same tightly packed
/// convention its destination -- a genuinely tight stage-IO output array
/// -- requires) is the only *actual* pattern this pass has ever been
/// confirmed to fix: this global's *only* other users are exactly such
/// tight `i8`-GEPs. A `spirv.MatrixType`-turned-`!llvm.array<N x vecM>`
/// global (roadmap L99), by contrast, is read back through `m[i][j]`
/// indexing lowered by MLIR upstream's own generic, natural-ABI-strided
/// `AccessChainOp` conversion -- so rewriting *its* init store to a tight
/// offset would only introduce the very "write one layout, read a
/// different one" corruption this pass exists to fix, not avoid it. This
/// check makes that distinction directly, from the real users this
/// particular global actually has, rather than assuming every
/// `Private`/`Function`-storage narrow-vector-array global is read back
/// the same way.
bool hasTightGEPUser(GlobalVariable &GV) {
  for (User *U : GV.users()) {
    // A GEP into a global constant reference (as opposed to a real,
    // in-function `getelementptr` instruction) usually appears as a
    // `ConstantExpr` folded directly into whatever instruction uses it
    // (e.g. `load <3 x i32>, ptr getelementptr (i8, ptr @g, i64 12)`) --
    // `GEPOperator` matches both that and a real `GetElementPtrInst`
    // uniformly, so this check catches either shape.
    auto *GEP = dyn_cast<GEPOperator>(U);
    if (GEP && GEP->getSourceElementType()->isIntegerTy(8) &&
        GEP->getPointerOperand() == &GV)
      return true;
  }
  return false;
}

} // namespace

PreservedAnalyses LocalNarrowVectorArrayInitPass::run(Module &M,
                                                       ModuleAnalysisManager &) {
  const DataLayout &DL = M.getDataLayout();
  bool Changed = false;
  for (GlobalVariable &GV : M.globals()) {
    // Deliberately restricted to `Private`/`Function`-storage globals
    // (address space 0); see the file comment in the header for why a
    // mesh entry's own stage-IO output array (address space 7/8) is never
    // actually affected, and why this guard is defense in depth rather
    // than load-bearing.
    if (GV.getAddressSpace() != 0)
      continue;
    auto *ArrTy = dyn_cast<ArrayType>(GV.getValueType());
    if (!ArrTy || !hasNarrowVectorArrayPaddingGap(ArrTy, DL))
      continue;
    // Only rewrite the init store if some other access to this exact
    // global already reads it back through a tight offset -- see
    // `hasTightGEPUser`'s own comment for why a global read back through
    // an ordinary, natural-ABI-strided `getelementptr` instead (e.g. a
    // `spirv.MatrixType`-turned array of columns, roadmap L99) must be
    // left alone, not "fixed" into disagreeing with its own real readers.
    if (!hasTightGEPUser(GV))
      continue;

    for (User *U : make_early_inc_range(GV.users())) {
      auto *SI = dyn_cast<StoreInst>(U);
      if (!SI || SI->getPointerOperand() != &GV)
        continue;
      auto *CV = dyn_cast<Constant>(SI->getValueOperand());
      if (!CV || CV->getType() != ArrTy)
        continue;

      IRBuilder<> Builder(SI);
      Type *I8Ty = Builder.getInt8Ty();
      uint64_t Offset = 0;
      Type *EltTy = ArrTy->getElementType();
      uint64_t EltTightSize = getTightSize(EltTy, DL);
      for (unsigned I = 0, E = ArrTy->getNumElements(); I != E; ++I) {
        Constant *Elt = CV->getAggregateElement(I);
        Value *Ptr = Offset == 0
                         ? static_cast<Value *>(&GV)
                         : Builder.CreateInBoundsGEP(
                               I8Ty, &GV, Builder.getInt64(Offset));
        Builder.CreateStore(Elt, Ptr);
        Offset += EltTightSize;
      }
      SI->eraseFromParent();
      Changed = true;
    }
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
