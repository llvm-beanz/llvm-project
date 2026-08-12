//===- WaveLowering.cpp - CPU target Phase 5: wave/builtin lowering ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap milestone 4's "builtin half" (see WaveLowering.h): every
// `feme.cpu.builtin.*` call reduces to the same flattened per-lane index,
//
//   flat = WaveIndex * W + lane        (lane = 0, 1, ..., W-1, a constant
//                                        iota -- feme::cpu::LaneIndex)
//
// decomposed into the thread-in-group id's x/y/z components the same way
// `feme::amdgpu::RaisedLoweringPass::lowerFlattenedThreadIDInGroup` combines
// them, just in reverse (division/remainder by the thread group dimensions
// instead of multiply/add):
//
//   x = flat % Gx             (feme::cpu::BuiltinCallKind::ThreadIdInGroup)
//   y = (flat / Gx) % Gy
//   z = flat / (Gx * Gy)
//
// and the dispatch-wide id (`ThreadId`) adds the group id, scaled by the
// group's own size, on top of that:
//
//   thread_id[c] = group_id[c] * NumThreads[c] + thread_id_in_group[c]
//
// This pass's whole job is building that arithmetic in `<W x i32>` and
// replacing each matched call with it; the remaining wave intrinsics
// (`WaveActiveSum`, ...) are milestone 8 and are left untouched.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/WaveLowering.h"

#include "feme/Transforms/CPU/BuiltinCalls.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// A constant `<W x i32>` of `0, 1, ..., W-1` -- the lane index within the
/// wave (`feme::cpu::BuiltinCallKind::LaneIndex`), and the basis every other
/// builtin's flattened index is built from.
Constant *getLaneIota(LLVMContext &Ctx, unsigned WaveSize) {
  SmallVector<Constant *, 32> Lanes;
  Type *I32Ty = Type::getInt32Ty(Ctx);
  for (unsigned I = 0; I != WaveSize; ++I)
    Lanes.push_back(ConstantInt::get(I32Ty, I));
  return ConstantVector::get(Lanes);
}

/// Builds `WaveIndex * W + lane` in `<W x i32>` (see the file comment
/// above): the flattened thread id within the group, for every lane of the
/// wave at once.
Value *buildFlattenedThreadIdInGroup(IRBuilder<> &Builder, Value *WaveIndex,
                                     unsigned WaveSize) {
  Value *Base = Builder.CreateMul(WaveIndex, Builder.getInt32(WaveSize));
  Value *WideBase = Builder.CreateVectorSplat(WaveSize, Base);
  return Builder.CreateAdd(WideBase,
                           getLaneIota(Builder.getContext(), WaveSize));
}

/// Decomposes \p Flat (see `buildFlattenedThreadIdInGroup`) into thread group
/// dimension \p Component's (0/1/2 for x/y/z) thread-in-group id, per the
/// file comment above.
Value *decomposeComponent(IRBuilder<> &Builder, Value *Flat, unsigned Component,
                          uint32_t NumThreadsX, uint32_t NumThreadsY) {
  switch (Component) {
  case 0:
    return Builder.CreateURem(
        Flat, Builder.CreateVectorSplat(
                  cast<FixedVectorType>(Flat->getType())->getNumElements(),
                  Builder.getInt32(NumThreadsX)));
  case 1: {
    unsigned W = cast<FixedVectorType>(Flat->getType())->getNumElements();
    Value *DivX = Builder.CreateUDiv(
        Flat, Builder.CreateVectorSplat(W, Builder.getInt32(NumThreadsX)));
    return Builder.CreateURem(
        DivX, Builder.CreateVectorSplat(W, Builder.getInt32(NumThreadsY)));
  }
  case 2: {
    unsigned W = cast<FixedVectorType>(Flat->getType())->getNumElements();
    uint64_t XY = static_cast<uint64_t>(NumThreadsX) * NumThreadsY;
    return Builder.CreateUDiv(
        Flat, Builder.CreateVectorSplat(
                  W, ConstantInt::get(Builder.getInt32Ty(),
                                      static_cast<uint32_t>(XY))));
  }
  default:
    llvm_unreachable("component out of range");
  }
}

/// Lowers one matched `feme.cpu.builtin.*` call into the arithmetic the file
/// comment above describes, and replaces/erases the call.
void lowerBuiltinCall(const MatchedBuiltinCall &Matched) {
  CallInst &CI = *Matched.Call;
  IRBuilder<> Builder(&CI);
  unsigned W = Matched.WaveSize;

  Value *Result;
  switch (Matched.Kind) {
  case BuiltinCallKind::LaneIndex:
    Result = getLaneIota(Builder.getContext(), W);
    break;
  case BuiltinCallKind::FlattenedThreadIdInGroup:
    Result = buildFlattenedThreadIdInGroup(Builder, Matched.Env.WaveIndex, W);
    break;
  case BuiltinCallKind::ThreadIdInGroup: {
    Value *Flat =
        buildFlattenedThreadIdInGroup(Builder, Matched.Env.WaveIndex, W);
    Result = decomposeComponent(Builder, Flat, Matched.Component,
                                Matched.NumThreadsX, Matched.NumThreadsY);
    break;
  }
  case BuiltinCallKind::ThreadId: {
    Value *Flat =
        buildFlattenedThreadIdInGroup(Builder, Matched.Env.WaveIndex, W);
    Value *InGroup =
        decomposeComponent(Builder, Flat, Matched.Component,
                           Matched.NumThreadsX, Matched.NumThreadsY);
    Value *GroupIDComponent = Matched.Component == 0   ? Matched.Env.GroupIDX
                              : Matched.Component == 1 ? Matched.Env.GroupIDY
                                                       : Matched.Env.GroupIDZ;
    uint32_t NumThreadsComponent = Matched.Component == 0 ? Matched.NumThreadsX
                                   : Matched.Component == 1
                                       ? Matched.NumThreadsY
                                       : Matched.NumThreadsZ;
    Value *Scaled = Builder.CreateMul(GroupIDComponent,
                                      Builder.getInt32(NumThreadsComponent));
    Value *WideScaled = Builder.CreateVectorSplat(W, Scaled);
    Result = Builder.CreateAdd(WideScaled, InGroup);
    break;
  }
  }

  Result->takeName(&CI);
  CI.replaceAllUsesWith(Result);
  CI.eraseFromParent();
}

} // namespace

PreservedAnalyses WaveLoweringPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration())
      continue;
    for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
      auto *CI = dyn_cast<CallInst>(&I);
      if (!CI)
        continue;
      std::optional<MatchedBuiltinCall> Matched = matchBuiltinCall(*CI);
      if (!Matched)
        continue;
      lowerBuiltinCall(*Matched);
      Changed = true;
    }
  }

  // A `feme.cpu.builtin.*` declaration left behind once its last caller is
  // rewritten away has nothing left to select it.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        F.getName().starts_with("feme.cpu.builtin."))
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
