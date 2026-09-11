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
