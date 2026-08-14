//===- WaveCalls.cpp - `feme.cpu.wave.*` call helpers --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/WaveCalls.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// The name each `WaveCallKind` mangles onto, before the (optional) operand
/// type suffix and the `.vW` wave-size suffix (see `mangleWaveCallName`).
StringRef kindName(WaveCallKind Kind) {
  switch (Kind) {
  case WaveCallKind::GetLaneCount:
    return "feme.cpu.wave.get_lane_count";
  case WaveCallKind::IsFirstLane:
    return "feme.cpu.wave.is_first_lane";
  case WaveCallKind::Any:
    return "feme.cpu.wave.any";
  case WaveCallKind::All:
    return "feme.cpu.wave.all";
  case WaveCallKind::AllEqual:
    return "feme.cpu.wave.all_equal";
  case WaveCallKind::ReadLane:
    return "feme.cpu.wave.readlane";
  case WaveCallKind::ActiveCountBits:
    return "feme.cpu.wave.active_countbits";
  case WaveCallKind::PrefixBitCount:
    return "feme.cpu.wave.prefix_bitcount";
  case WaveCallKind::Ballot:
    return "feme.cpu.wave.ballot";
  case WaveCallKind::ActiveSum:
    return "feme.cpu.wave.active_sum";
  case WaveCallKind::ActiveProduct:
    return "feme.cpu.wave.active_product";
  case WaveCallKind::ActiveMax:
    return "feme.cpu.wave.active_max";
  case WaveCallKind::ActiveUMax:
    return "feme.cpu.wave.active_umax";
  case WaveCallKind::ActiveMin:
    return "feme.cpu.wave.active_min";
  case WaveCallKind::ActiveUMin:
    return "feme.cpu.wave.active_umin";
  case WaveCallKind::ActiveBitAnd:
    return "feme.cpu.wave.active_bitand";
  case WaveCallKind::ActiveBitOr:
    return "feme.cpu.wave.active_bitor";
  case WaveCallKind::ActiveBitXor:
    return "feme.cpu.wave.active_bitxor";
  case WaveCallKind::PrefixSum:
    return "feme.cpu.wave.prefix_sum";
  case WaveCallKind::PrefixProduct:
    return "feme.cpu.wave.prefix_product";
  }
  llvm_unreachable("unknown WaveCallKind");
}

std::optional<WaveCallKind> parseKindName(StringRef Name) {
  if (Name == "feme.cpu.wave.get_lane_count")
    return WaveCallKind::GetLaneCount;
  if (Name == "feme.cpu.wave.is_first_lane")
    return WaveCallKind::IsFirstLane;
  if (Name == "feme.cpu.wave.any")
    return WaveCallKind::Any;
  if (Name == "feme.cpu.wave.all")
    return WaveCallKind::All;
  if (Name == "feme.cpu.wave.all_equal")
    return WaveCallKind::AllEqual;
  if (Name == "feme.cpu.wave.readlane")
    return WaveCallKind::ReadLane;
  if (Name == "feme.cpu.wave.active_countbits")
    return WaveCallKind::ActiveCountBits;
  if (Name == "feme.cpu.wave.prefix_bitcount")
    return WaveCallKind::PrefixBitCount;
  if (Name == "feme.cpu.wave.ballot")
    return WaveCallKind::Ballot;
  if (Name == "feme.cpu.wave.active_sum")
    return WaveCallKind::ActiveSum;
  if (Name == "feme.cpu.wave.active_product")
    return WaveCallKind::ActiveProduct;
  if (Name == "feme.cpu.wave.active_max")
    return WaveCallKind::ActiveMax;
  if (Name == "feme.cpu.wave.active_umax")
    return WaveCallKind::ActiveUMax;
  if (Name == "feme.cpu.wave.active_min")
    return WaveCallKind::ActiveMin;
  if (Name == "feme.cpu.wave.active_umin")
    return WaveCallKind::ActiveUMin;
  if (Name == "feme.cpu.wave.active_bitand")
    return WaveCallKind::ActiveBitAnd;
  if (Name == "feme.cpu.wave.active_bitor")
    return WaveCallKind::ActiveBitOr;
  if (Name == "feme.cpu.wave.active_bitxor")
    return WaveCallKind::ActiveBitXor;
  if (Name == "feme.cpu.wave.prefix_sum")
    return WaveCallKind::PrefixSum;
  if (Name == "feme.cpu.wave.prefix_product")
    return WaveCallKind::PrefixProduct;
  return std::nullopt;
}

/// Whether \p Kind's value operand is type-overloaded (`AllEqual`/
/// `ReadLane`, which operate on whatever scalar type `T` the source
/// intrinsic was called with, plus the roadmap step R4 reduce/scan kinds
/// -- `Active{Sum,Product,Max,UMax,Min,UMin,BitAnd,BitOr,BitXor}` and
/// `Prefix{Sum,Product}`, all of which likewise share DXIL's `OverloadTy`)
/// rather than always `i1` (`Any`/`All`/`ActiveCountBits`/`PrefixBitCount`)
/// or absent (`GetLaneCount`/`IsFirstLane`).
bool hasTypeOverloadedOperand(WaveCallKind Kind) {
  return Kind == WaveCallKind::AllEqual || Kind == WaveCallKind::ReadLane ||
         Kind == WaveCallKind::ActiveSum ||
         Kind == WaveCallKind::ActiveProduct ||
         Kind == WaveCallKind::ActiveMax || Kind == WaveCallKind::ActiveUMax ||
         Kind == WaveCallKind::ActiveMin || Kind == WaveCallKind::ActiveUMin ||
         Kind == WaveCallKind::ActiveBitAnd ||
         Kind == WaveCallKind::ActiveBitOr ||
         Kind == WaveCallKind::ActiveBitXor ||
         Kind == WaveCallKind::PrefixSum || Kind == WaveCallKind::PrefixProduct;
}

bool hasMask(WaveCallKind Kind) { return Kind != WaveCallKind::GetLaneCount; }

bool hasOperand(WaveCallKind Kind) {
  return Kind != WaveCallKind::GetLaneCount &&
         Kind != WaveCallKind::IsFirstLane;
}

bool hasLaneIndex(WaveCallKind Kind) { return Kind == WaveCallKind::ReadLane; }

/// Appends the scalar type mangling used for a type-overloaded operand,
/// e.g. `f32`, `i32`, `i1` -- same convention `ResourceCalls` uses.
void appendScalarMangling(raw_ostream &OS, Type *Ty) {
  if (Ty->isHalfTy()) {
    OS << "f16";
  } else if (Ty->isFloatTy()) {
    OS << "f32";
  } else if (Ty->isDoubleTy()) {
    OS << "f64";
  } else if (Ty->isIntegerTy()) {
    OS << "i" << Ty->getIntegerBitWidth();
  } else {
    llvm_unreachable("unsupported feme.cpu.wave.* operand type");
  }
}

/// Returns the type-mangled `feme.cpu.wave.*` name for \p Kind, \p WaveSize
/// and (if \p Kind is type-overloaded) \p ElementType.
std::string mangleWaveCallName(WaveCallKind Kind, unsigned WaveSize,
                               Type *ElementType) {
  std::string Name;
  raw_string_ostream OS(Name);
  OS << kindName(Kind);
  if (hasTypeOverloadedOperand(Kind)) {
    OS << '.';
    appendScalarMangling(OS, ElementType);
  }
  OS << ".v" << WaveSize;
  return Name;
}

} // namespace

namespace feme::cpu {

bool isDivergentWaveCallResult(WaveCallKind Kind) {
  return Kind == WaveCallKind::IsFirstLane ||
         Kind == WaveCallKind::PrefixBitCount ||
         Kind == WaveCallKind::PrefixSum || Kind == WaveCallKind::PrefixProduct;
}

CallInst *createWaveCall(IRBuilderBase &Builder, WaveCallKind Kind,
                         unsigned WaveSize, Value *WideMask, Value *WideOperand,
                         Value *WideLaneIndex, const Twine &Name) {
  assert(hasMask(Kind) == (WideMask != nullptr) &&
         "WideMask must be given iff Kind uses one");
  assert(hasOperand(Kind) == (WideOperand != nullptr) &&
         "WideOperand must be given iff Kind uses one");
  assert(hasLaneIndex(Kind) == (WideLaneIndex != nullptr) &&
         "WideLaneIndex must be given iff Kind uses one");

  Module *M = Builder.GetInsertBlock()->getModule();
  LLVMContext &Ctx = M->getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I1Ty = Type::getInt1Ty(Ctx);

  SmallVector<Type *, 4> ParamTypes;
  SmallVector<Value *, 4> Args;
  if (hasMask(Kind)) {
    ParamTypes.push_back(WideMask->getType());
    Args.push_back(WideMask);
  }
  if (hasOperand(Kind)) {
    ParamTypes.push_back(WideOperand->getType());
    Args.push_back(WideOperand);
  }
  if (hasLaneIndex(Kind)) {
    ParamTypes.push_back(WideLaneIndex->getType());
    Args.push_back(WideLaneIndex);
  }

  Type *ElementType =
      hasTypeOverloadedOperand(Kind)
          ? cast<VectorType>(WideOperand->getType())->getElementType()
          : nullptr;

  Type *RetTy;
  switch (Kind) {
  case WaveCallKind::GetLaneCount:
  case WaveCallKind::ActiveCountBits:
    RetTy = I32Ty;
    break;
  case WaveCallKind::IsFirstLane:
    RetTy = FixedVectorType::get(I1Ty, WaveSize);
    break;
  case WaveCallKind::Any:
  case WaveCallKind::All:
  case WaveCallKind::AllEqual:
    RetTy = I1Ty;
    break;
  case WaveCallKind::ReadLane:
    // A genuine per-lane `<W x T>` gather, not a uniform broadcast: the
    // lane index is not required to be uniform across the wave (see
    // `WaveCallKind::ReadLane`'s comment), so each output lane may read a
    // different source lane.
    RetTy = FixedVectorType::get(ElementType, WaveSize);
    break;
  case WaveCallKind::PrefixBitCount:
    RetTy = FixedVectorType::get(I32Ty, WaveSize);
    break;
  case WaveCallKind::Ballot:
    // DXIL's fixed `uint4` ballot ABI (`%dx.types.fouri32`): a literal,
    // unnamed struct so it structurally unifies with whatever anonymous
    // struct type `llvm.dx.wave.ballot`'s multi-value return actually uses
    // (see `feme::dxil::OpRaisingPass::raiseAggregateCall`'s comment for why
    // a *named* struct wouldn't type-check the same way here).
    RetTy = StructType::get(Ctx, {I32Ty, I32Ty, I32Ty, I32Ty});
    break;
  case WaveCallKind::ActiveSum:
  case WaveCallKind::ActiveProduct:
  case WaveCallKind::ActiveMax:
  case WaveCallKind::ActiveUMax:
  case WaveCallKind::ActiveMin:
  case WaveCallKind::ActiveUMin:
  case WaveCallKind::ActiveBitAnd:
  case WaveCallKind::ActiveBitOr:
  case WaveCallKind::ActiveBitXor:
    // A reduction over the operand's own type, uniform across the wave
    // (roadmap step R4).
    RetTy = ElementType;
    break;
  case WaveCallKind::PrefixSum:
  case WaveCallKind::PrefixProduct:
    // An exclusive scan: a genuine per-lane `<W x T>` value, like
    // `PrefixBitCount` but overloaded on the operand's type instead of a
    // fixed `i32`.
    RetTy = FixedVectorType::get(ElementType, WaveSize);
    break;
  }

  std::string MangledName = mangleWaveCallName(Kind, WaveSize, ElementType);
  Function *Callee = M->getFunction(MangledName);
  if (!Callee) {
    FunctionType *FTy = FunctionType::get(RetTy, ParamTypes, false);
    Callee =
        Function::Create(FTy, GlobalValue::ExternalLinkage, MangledName, M);
    Callee->setDoesNotAccessMemory();
    Callee->setWillReturn();
  }
  return Builder.CreateCall(Callee, Args, Name);
}

std::optional<MatchedWaveCall> matchWaveCall(const CallInst &CI) {
  const Function *Callee = CI.getCalledFunction();
  if (!Callee)
    return std::nullopt;

  StringRef Name = Callee->getName();
  auto [Base, WaveSizeStr] = Name.rsplit(".v");
  unsigned WaveSize = 0;
  if (WaveSizeStr.empty() || WaveSizeStr.getAsInteger(10, WaveSize))
    return std::nullopt;

  // A non-type-overloaded kind's name matches `Base` outright; a
  // type-overloaded one (`AllEqual`/`ReadLane`) instead has its element-type
  // suffix (e.g. `.i32`) still attached, so retry with that suffix stripped.
  std::optional<WaveCallKind> Kind = parseKindName(Base);
  if (!Kind) {
    StringRef KindBase = Base.rsplit('.').first;
    Kind = parseKindName(KindBase);
  }
  if (!Kind)
    return std::nullopt;

  MatchedWaveCall Result;
  Result.Kind = *Kind;
  Result.Call = const_cast<CallInst *>(&CI);
  Result.WaveSize = WaveSize;

  unsigned OperandIdx = 0;
  if (hasMask(*Kind))
    Result.WideMask = CI.getArgOperand(OperandIdx++);
  if (hasOperand(*Kind))
    Result.WideOperand = CI.getArgOperand(OperandIdx++);
  if (hasLaneIndex(*Kind))
    Result.WideLaneIndex = CI.getArgOperand(OperandIdx++);
  return Result;
}

} // namespace feme::cpu
