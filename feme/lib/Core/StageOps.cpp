//===- StageOps.cpp - Canonical `feme.stage.*` operation family ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Core/StageOps.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme;

namespace {

struct StageOpInfo {
  StageOpKind Kind;
  StringLiteral Name;
  bool Overloaded;
};

// clang-format off
constexpr StageOpInfo StageOpTable[] = {
    {StageOpKind::InputLoad, "feme.stage.input.load", true},
    {StageOpKind::OutputStore, "feme.stage.output.store", true},
    {StageOpKind::Discard, "feme.stage.discard", false},
    {StageOpKind::Demote, "feme.stage.demote", false},
    {StageOpKind::IsHelper, "feme.stage.is_helper", false},
    {StageOpKind::DerivativeXFine, "feme.stage.derivative.x.fine", true},
    {StageOpKind::DerivativeYFine, "feme.stage.derivative.y.fine", true},
    {StageOpKind::DerivativeXCoarse, "feme.stage.derivative.x.coarse", true},
    {StageOpKind::DerivativeYCoarse, "feme.stage.derivative.y.coarse", true},
    {StageOpKind::QuadRead, "feme.stage.quad.read", true},
    {StageOpKind::InterpolateAtCentroid, "feme.stage.interpolate.at.centroid",
     true},
    {StageOpKind::InterpolateAtSample, "feme.stage.interpolate.at.sample",
     true},
    {StageOpKind::InterpolateAtOffset, "feme.stage.interpolate.at.offset",
     true},
    {StageOpKind::StreamEmit, "feme.stage.stream.emit", false},
    {StageOpKind::StreamCut, "feme.stage.stream.cut", false},
    {StageOpKind::SubpassLoad, "feme.stage.subpass.load", false},
};
// clang-format on

static_assert(std::size(StageOpTable) ==
                  static_cast<size_t>(StageOpKind::NumStageOpKinds),
              "StageOpTable must cover every StageOpKind");

const StageOpInfo &getStageOpInfo(StageOpKind Kind) {
  return StageOpTable[static_cast<unsigned>(Kind)];
}

/// A short, unambiguous type suffix for \p Ty, in the same spirit as
/// `dx.op.*`'s own per-type callee suffix (e.g. `dx.op.loadInput.f32`):
/// scalar element type/width, prefixed with the vector length when \p Ty is
/// a fixed vector.
void appendTypeSuffix(SmallVectorImpl<char> &Out, Type *Ty) {
  raw_svector_ostream OS(Out);
  Type *Scalar = Ty;
  if (auto *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    OS << 'v' << VecTy->getNumElements();
    Scalar = VecTy->getElementType();
  }
  if (Scalar->isFloatTy())
    OS << "f32";
  else if (Scalar->isDoubleTy())
    OS << "f64";
  else if (Scalar->isHalfTy())
    OS << "f16";
  else if (auto *IntTy = dyn_cast<IntegerType>(Scalar))
    OS << 'i' << IntTy->getBitWidth();
  else
    OS << "unknown";
}

} // namespace

bool feme::isStageOpKindOverloaded(StageOpKind Kind) {
  return getStageOpInfo(Kind).Overloaded;
}

StringRef feme::getStageOpName(StageOpKind Kind) {
  return getStageOpInfo(Kind).Name;
}

std::optional<StageOpKind> feme::getStageOpKind(const Function &F) {
  StringRef Name = F.getName();
  for (const StageOpInfo &Info : StageOpTable) {
    if (Name == Info.Name)
      return Info.Kind;
    // An overloaded op's declaration is named "<prefix>.<suffix>"; match the
    // prefix up to (and including) the separating '.'.
    if (Info.Overloaded && Name.starts_with(Info.Name) &&
        Name.size() > Info.Name.size() && Name[Info.Name.size()] == '.')
      return Info.Kind;
  }
  return std::nullopt;
}

bool feme::isStageOpCall(const CallInst &CI, StageOpKind *Kind) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return false;
  std::optional<StageOpKind> Found = getStageOpKind(*Callee);
  if (!Found)
    return false;
  if (Kind)
    *Kind = *Found;
  return true;
}

FunctionCallee feme::getOrInsertStageOp(Module &M, StageOpKind Kind,
                                        Type *ResultTy,
                                        ArrayRef<Type *> ArgTys) {
  const StageOpInfo &Info = getStageOpInfo(Kind);
  SmallString<64> Name(Info.Name);
  if (Info.Overloaded) {
    Name.push_back('.');
    // The type that varies across an overload of this op is its result, for
    // every overloaded kind except `OutputStore`, whose only "value" is its
    // (void-returning) `value` operand -- the fourth argument, per
    // `StageOpKind::OutputStore`'s comment.
    Type *OverloadTy = Kind == StageOpKind::OutputStore ? ArgTys[3] : ResultTy;
    appendTypeSuffix(Name, OverloadTy);
  }
  FunctionType *FTy = FunctionType::get(ResultTy, ArgTys, /*isVarArg=*/false);
  return M.getOrInsertFunction(Name, FTy);
}

static CallInst *createCall(IRBuilderBase &B, StageOpKind Kind, Type *ResultTy,
                            ArrayRef<Value *> Args, const Twine &Name = "") {
  Module *M = B.GetInsertBlock()->getModule();
  SmallVector<Type *, 8> ArgTys;
  for (Value *Arg : Args)
    ArgTys.push_back(Arg->getType());
  FunctionCallee Callee = getOrInsertStageOp(*M, Kind, ResultTy, ArgTys);
  return B.CreateCall(Callee, Args, Name);
}

CallInst *feme::createStageInputLoad(IRBuilderBase &B, Type *ResultTy,
                                     uint32_t ElementID, Value *Row,
                                     Value *Component, Value *Vertex,
                                     const Twine &Name) {
  Type *I32 = B.getInt32Ty();
  Value *Element = ConstantInt::get(I32, ElementID);
  return createCall(B, StageOpKind::InputLoad, ResultTy,
                    {Element, Row, Component, Vertex}, Name);
}

CallInst *feme::createStageOutputStore(IRBuilderBase &B, uint32_t ElementID,
                                       Value *Row, Value *Component, Value *Val,
                                       Value *Vertex) {
  Type *I32 = B.getInt32Ty();
  Value *Element = ConstantInt::get(I32, ElementID);
  return createCall(B, StageOpKind::OutputStore, B.getVoidTy(),
                    {Element, Row, Component, Val, Vertex});
}

CallInst *feme::createStageDiscard(IRBuilderBase &B, Value *Condition) {
  return createCall(B, StageOpKind::Discard, B.getVoidTy(), {Condition});
}

CallInst *feme::createStageDemote(IRBuilderBase &B, Value *Condition) {
  return createCall(B, StageOpKind::Demote, B.getVoidTy(), {Condition});
}

CallInst *feme::createStageIsHelper(IRBuilderBase &B) {
  return createCall(B, StageOpKind::IsHelper, B.getInt1Ty(), {});
}

CallInst *feme::createStageDerivative(IRBuilderBase &B, StageOpKind Kind,
                                      Value *Val) {
  assert((Kind == StageOpKind::DerivativeXFine ||
          Kind == StageOpKind::DerivativeYFine ||
          Kind == StageOpKind::DerivativeXCoarse ||
          Kind == StageOpKind::DerivativeYCoarse) &&
         "not a derivative StageOpKind");
  return createCall(B, Kind, Val->getType(), {Val});
}

CallInst *feme::createStageQuadRead(IRBuilderBase &B, Value *Val,
                                    uint8_t Direction) {
  Value *Dir = ConstantInt::get(B.getInt8Ty(), Direction);
  return createCall(B, StageOpKind::QuadRead, Val->getType(), {Val, Dir});
}

CallInst *feme::createStageInterpolateAtCentroid(IRBuilderBase &B,
                                                 Type *ResultTy,
                                                 uint32_t ElementID,
                                                 Value *Component) {
  Value *Element = ConstantInt::get(B.getInt32Ty(), ElementID);
  return createCall(B, StageOpKind::InterpolateAtCentroid, ResultTy,
                    {Element, Component});
}

CallInst *feme::createStageInterpolateAtSample(IRBuilderBase &B, Type *ResultTy,
                                               uint32_t ElementID,
                                               Value *Component,
                                               Value *Sample) {
  Value *Element = ConstantInt::get(B.getInt32Ty(), ElementID);
  return createCall(B, StageOpKind::InterpolateAtSample, ResultTy,
                    {Element, Component, Sample});
}

CallInst *feme::createStageInterpolateAtOffset(IRBuilderBase &B, Type *ResultTy,
                                               uint32_t ElementID,
                                               Value *Component, Value *OffsetX,
                                               Value *OffsetY) {
  Value *Element = ConstantInt::get(B.getInt32Ty(), ElementID);
  return createCall(B, StageOpKind::InterpolateAtOffset, ResultTy,
                    {Element, Component, OffsetX, OffsetY});
}

CallInst *feme::createStageStreamEmit(IRBuilderBase &B, uint32_t Stream) {
  Value *StreamVal = ConstantInt::get(B.getInt32Ty(), Stream);
  return createCall(B, StageOpKind::StreamEmit, B.getVoidTy(), {StreamVal});
}

CallInst *feme::createStageStreamCut(IRBuilderBase &B, uint32_t Stream) {
  Value *StreamVal = ConstantInt::get(B.getInt32Ty(), Stream);
  return createCall(B, StageOpKind::StreamCut, B.getVoidTy(), {StreamVal});
}

CallInst *feme::createStageSubpassLoad(IRBuilderBase &B,
                                       uint32_t AttachmentIndex,
                                       uint32_t Component) {
  Value *IndexVal = ConstantInt::get(B.getInt32Ty(), AttachmentIndex);
  Value *ComponentVal = ConstantInt::get(B.getInt32Ty(), Component);
  return createCall(B, StageOpKind::SubpassLoad, B.getFloatTy(),
                    {IndexVal, ComponentVal});
}

std::optional<uint64_t> feme::getStageOpConstantOperand(const CallInst &CI,
                                                        unsigned Idx) {
  if (Idx >= CI.arg_size())
    return std::nullopt;
  const auto *CInt = dyn_cast<ConstantInt>(CI.getArgOperand(Idx));
  if (!CInt)
    return std::nullopt;
  return CInt->getZExtValue();
}
