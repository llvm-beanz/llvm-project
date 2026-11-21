//===- RootConstantLowering.cpp - CPU target root constant lowering -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/RootConstantLowering.h"
#include "feme/Transforms/DXIL/OpRaising.h"
#include "feme/Transforms/DXIL/RootBindingPlan.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include <algorithm>
#include <limits>
#include <optional>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// A full DXIL cbuffer row is always 16 bytes, regardless of how many
/// components of what width it is split into (see
/// `int_dx_resource_load_cbufferrow_4`'s TableGen comment: "The total size
/// of the return should always be 128 bits").
constexpr uint32_t RowSizeBytes = 16;
constexpr StringLiteral ExplicitBindingMD = "feme.cpu.rootconstantbinding";

struct ExplicitBinding {
  uint32_t Space;
  uint32_t Register;
  uint32_t Size;
};

std::optional<ExplicitBinding> getExplicitBinding(const Function &F) {
  auto *MD = F.getMetadata(ExplicitBindingMD);
  if (!MD || MD->getNumOperands() != 3)
    return std::nullopt;
  uint32_t Values[3];
  for (unsigned I = 0; I != 3; ++I) {
    auto *Value = mdconst::dyn_extract<ConstantInt>(MD->getOperand(I));
    if (!Value || !Value->getType()->isIntegerTy(32))
      return std::nullopt;
    Values[I] = Value->getZExtValue();
  }
  if (!Values[2] || Values[2] > 64 * sizeof(uint32_t) ||
      Values[2] % sizeof(uint32_t))
    return std::nullopt;
  return ExplicitBinding{Values[0], Values[1], Values[2]};
}

/// DXIL's sentinel `RangeSize` for an unbounded array binding
/// (`register(bN[])`, no upper bound). Such a binding has no fixed
/// advertised size to report or bounds-check reads against, so it is never
/// a candidate root-constant binding (see the header comment).
constexpr uint32_t UnboundedRangeSize = ~0u;

/// Returns the intrinsic ID of the call \p V is, or `not_intrinsic`.
Intrinsic::ID getIntrinsicID(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee ? Callee->getIntrinsicID() : Intrinsic::not_intrinsic;
}

/// Whether \p F contains any bindless (`handlefromheap`) resource access --
/// see the header comment for the two different ways such a function's
/// root-constant access ends up lowered.
bool usesResourceHeap(Function &F) {
  for (Instruction &I : instructions(F))
    if (getIntrinsicID(&I) == Intrinsic::dx_resource_handlefromheap)
      return true;
  return false;
}

/// Returns \p Handle's declared binding shape (source register space/base
/// register, declared array length, and one array element's declared byte
/// size) if it is a `dx.CBuffer` handle whose binding this milestone can
/// represent (see the header comment: any finite, statically-known
/// binding), or `std::nullopt` otherwise (a non-`dx.CBuffer` handle, an
/// unbounded range, or a non-constant space/register/range-size -- none of
/// which DXIL ever actually produces for these operands, but this pass
/// still declines rather than assumes).
std::optional<RootConstantAccess> matchRootConstantHandle(CallInst &Handle) {
  auto *HandleTy = dyn_cast<TargetExtType>(Handle.getType());
  if (!HandleTy || HandleTy->getName() != "dx.CBuffer")
    return std::nullopt;

  auto *SpaceC = dyn_cast<ConstantInt>(Handle.getArgOperand(0));
  auto *RegisterC = dyn_cast<ConstantInt>(Handle.getArgOperand(1));
  auto *RangeSizeC = dyn_cast<ConstantInt>(Handle.getArgOperand(2));
  if (!SpaceC || !RegisterC || !RangeSizeC)
    return std::nullopt;

  uint64_t RangeSize = RangeSizeC->getZExtValue();
  if (RangeSize == 0 || RangeSize == UnboundedRangeSize)
    return std::nullopt;

  // The handle type's own array-of-bytes type parameter (e.g. `[32 x i8]`
  // for `target("dx.CBuffer", [32 x i8])`) is one array element's declared
  // byte size.
  auto *ElemArrayTy = dyn_cast<ArrayType>(HandleTy->getTypeParameter(0));
  if (!ElemArrayTy || !ElemArrayTy->getElementType()->isIntegerTy(8))
    return std::nullopt;
  uint64_t ElementSize = ElemArrayTy->getNumElements();
  if (ElementSize > std::numeric_limits<uint32_t>::max() / RangeSize)
    return std::nullopt;

  RootConstantAccess Access;
  Access.Handle = &Handle;
  Access.Space = static_cast<uint32_t>(SpaceC->getZExtValue());
  Access.Register = static_cast<uint32_t>(RegisterC->getZExtValue());
  Access.RangeSize = static_cast<uint32_t>(RangeSize);
  Access.ElementSize = static_cast<uint32_t>(ElementSize);
  return Access;
}

/// Collects homogeneous 128-bit rows (2/4/8 components) through \p Handle,
/// or `std::nullopt` if any use is unsupported -- in which case
/// \p Handle is left entirely alone, for
/// `feme::cpu::checkSupportedRaisedOps` to reject exactly as before this
/// pass existed, rather than partially rewriting it. A row index need not
/// be constant (roadmap R25); it is kept as whatever `Value` the load
/// already uses.
std::optional<SmallVector<RootConstantRowLoad, 4>>
collectRowLoads(CallInst &Handle) {
  SmallVector<RootConstantRowLoad, 4> Loads;
  for (User *U : Handle.users()) {
    auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      return std::nullopt;
    auto *RowTy = dyn_cast<StructType>(CI->getType());
    Intrinsic::ID RowID = RowTy ? feme::dxil::getCBufferRowIntrinsic(*RowTy)
                                : Intrinsic::not_intrinsic;
    if (RowID == Intrinsic::not_intrinsic || RowID != getIntrinsicID(CI) ||
        CI->getArgOperand(0) != &Handle)
      return std::nullopt;
    Loads.push_back(RootConstantRowLoad{CI, CI->getArgOperand(1)});
  }
  return Loads;
}

/// Builds \p F's replacement: the same function with `root_constants`
/// (`ptr`) and `root_constant_size` (`i32`) appended, mirroring
/// `feme::cpu::ResourceCallEnv`'s fields of the same name so
/// `feme::cpu::EntryWrapperPass`'s existing by-name argument wiring (see
/// EntryWrapper.cpp) needs no changes to recognize them. The body is moved
/// across (not cloned), exactly as `feme::cpu::ResourceLoweringPass`'s own
/// `addResourceEnvParams` does for its differently-shaped parameter list.
/// Only called for a function `matchRootConstantAccess` accepted that has
/// no bindless resource access of its own (see the header comment) --
/// `feme::cpu::ResourceLoweringPass` reuses its own, already-added
/// parameters of the same name for the other case instead of calling this.
Function *addRootConstantParams(Function &F, Value *&RootConstants,
                                Value *&RootConstantSize) {
  LLVMContext &Ctx = F.getContext();
  SmallVector<Type *, 2> ParamTypes(F.getFunctionType()->params());
  ParamTypes.push_back(PointerType::get(Ctx, 0));
  ParamTypes.push_back(Type::getInt32Ty(Ctx));

  FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                          F.getFunctionType()->isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
  // `GlobalObject::copyAttributesFrom()` does not copy function-attached
  // metadata (e.g. `!feme.signature` attached by
  // `feme::graphics::CanonicalizeStagePass`) -- copy it explicitly so
  // stage reflection metadata survives this parameter-injection, exactly
  // like `feme::cpu::ResourceLoweringPass::addResourceEnvParams` and
  // `feme::cpu::addSubpassInputHeapParams` already do for their own
  // trailing-parameter replacements (roadmap H7o: a real
  // `dEQP-VK.pipeline.monolithic.multisample.min_sample_shading_*`
  // fragment shader reading only a push constant -- no bound resource --
  // was the first real case to reach this specific helper with a
  // `feme.stage.*` op needing that metadata back later, surfacing the gap
  // as `feme-cpu-wrap-fragment`'s "requires attached feme.signature
  // metadata" diagnostic).
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  for (auto [Kind, Node] : MDs)
    NewF->setMetadata(Kind, Node);
  NewF->setComdat(F.getComdat());
  NewF->splice(NewF->begin(), &F);

  for (auto [OldArg, NewArg] : llvm::zip(F.args(), NewF->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  auto ArgIt = NewF->arg_begin() + F.arg_size();
  RootConstants = &*ArgIt++;
  RootConstants->setName("root_constants");
  RootConstantSize = &*ArgIt++;
  RootConstantSize->setName("root_constant_size");

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

/// Attaches the same `!feme.cpu.resources` heap-usage metadata node
/// "Resource usage discovery" describes for \p F (see `attachResourceMetadata`
/// in ResourceLowering.cpp for the shape this mirrors): its name, the root
/// constant block size this function reads, `false` (a root-constant-only
/// function uses neither the sampler heap nor any statically-known dynamic
/// heap index), the binding's source register space and base register
/// (roadmap R25: any single binding is recognized now, so a host needs to
/// be told which one this is), a root-constant min offset (always 0: a
/// DXIL root constant's own register-bound view always starts its own span
/// at byte 0 -- see `feme::cpu::ResourceInfo::RootConstantMinOffset`'s own
/// comment, roadmap H6u), and no trailing indices.
void attachRootConstantMetadata(Function &F, uint32_t RootConstantSize,
                                uint32_t Space, uint32_t Register) {
  LLVMContext &Ctx = F.getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  MDNode *Node = MDNode::get(
      Ctx, {MDString::get(Ctx, F.getName()),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstantSize)),
            ConstantAsMetadata::get(ConstantInt::getFalse(Ctx)),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, Space)),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, Register)),
            ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0))});
  F.getParent()
      ->getOrInsertNamedMetadata("feme.cpu.resources")
      ->addOperand(Node);
}

/// Lowers \p F's root-constant access, if any and if \p F has no bindless
/// resource access of its own (see the header comment for the other case,
/// handled by `feme::cpu::ResourceLoweringPass` instead), returning the
/// rewritten function (a new one, since its signature grows), or nullptr
/// otherwise.
Function *lowerFunctionRootConstants(Function &F) {
  if (F.isDeclaration() || usesResourceHeap(F))
    return nullptr;
  if (!matchRootConstantAccess(F))
    return nullptr;

  Value *RootConstants;
  Value *RootConstantSize;
  Function *NewF = addRootConstantParams(F, RootConstants, RootConstantSize);

  // Re-matched against `NewF` rather than reusing the match against `F`
  // above: a dynamic row or array index (roadmap R25) may be one of `F`'s
  // own arguments, which `addRootConstantParams` has just replaced with
  // `NewF`'s corresponding one -- the match above only existed to decide
  // whether this function has anything to lower at all, cheaply, before
  // committing to rebuilding it.
  std::optional<RootConstantAccess> Access = matchRootConstantAccess(*NewF);
  uint32_t RootConstantSizeNeeded =
      lowerRootConstantAccess(*Access, RootConstants, RootConstantSize);
  attachRootConstantMetadata(*NewF, RootConstantSizeNeeded, Access->Space,
                             Access->Register);
  return NewF;
}

} // namespace

namespace feme::cpu {

Error selectRootSignatureConstants(Function &F, ShaderStage Stage) {
  using Visibility = dxbc::ShaderVisibility;
  using Flags = dxbc::RootFlags;
  Visibility Visible;
  Flags Denied;
  switch (Stage) {
  case ShaderStage::Compute:
    Visible = Visibility::All;
    Denied = Flags::None;
    break;
  case ShaderStage::Vertex:
    Visible = Visibility::Vertex;
    Denied = Flags::DenyVertexShaderRootAccess;
    break;
  case ShaderStage::Hull:
    Visible = Visibility::Hull;
    Denied = Flags::DenyHullShaderRootAccess;
    break;
  case ShaderStage::Domain:
    Visible = Visibility::Domain;
    Denied = Flags::DenyDomainShaderRootAccess;
    break;
  case ShaderStage::Geometry:
    Visible = Visibility::Geometry;
    Denied = Flags::DenyGeometryShaderRootAccess;
    break;
  case ShaderStage::Fragment:
    Visible = Visibility::Pixel;
    Denied = Flags::DenyPixelShaderRootAccess;
    break;
  case ShaderStage::Amplification:
    Visible = Visibility::Amplification;
    Denied = Flags::DenyAmplificationShaderRootAccess;
    break;
  case ShaderStage::Mesh:
    Visible = Visibility::Mesh;
    Denied = Flags::DenyMeshShaderRootAccess;
    break;
  case ShaderStage::Library:
  case ShaderStage::RayGeneration:
  case ShaderStage::Intersection:
  case ShaderStage::AnyHit:
  case ShaderStage::ClosestHit:
  case ShaderStage::Miss:
  case ShaderStage::Callable:
  case ShaderStage::NumStages:
    return createStringError(inconvertibleErrorCode(),
                             "unsupported stage for root-signature constants");
  }
  auto Plan = dxil::getRootBindingPlan(F);
  if (!Plan)
    return Plan.takeError();
  if (!*Plan)
    return createStringError(
        inconvertibleErrorCode(),
        "root-signature constants require a root signature");
  if ((*Plan)->getFlags() & uint32_t(Flags::LocalRootSignature))
    return createStringError(inconvertibleErrorCode(),
                             "local root signatures are not supported for CPU "
                             "root constants");
  if ((*Plan)->getFlags() & uint32_t(Denied))
    return createStringError(
        inconvertibleErrorCode(),
        "root signature denies access for the selected stage");
  const dxil::RootParameter *Selected = nullptr;
  for (const auto &Parameter : (*Plan)->getParameters()) {
    if (Parameter.Kind != dxbc::RootParameterType::Constants32Bit ||
        (Parameter.Visibility != Visibility::All &&
         Parameter.Visibility != Visible))
      continue;
    if (Selected)
      return createStringError(inconvertibleErrorCode(),
                               "root-signature constants support exactly one "
                               "visible constant parameter");
    Selected = &Parameter;
  }
  if (!Selected)
    return createStringError(inconvertibleErrorCode(),
                             "root signature has no inline constant parameter "
                             "visible to the selected stage");
  if (Error E = (*Plan)->checkShaderBindings(F, Visible))
    return E;
  const auto &Binding = Selected->Bindings.front();
  LLVMContext &C = F.getContext();
  auto Word = [&](uint32_t Value) {
    return ConstantAsMetadata::get(
        ConstantInt::get(Type::getInt32Ty(C), Value));
  };
  F.setMetadata(ExplicitBindingMD,
                MDNode::get(C, {Word(Binding.Space), Word(Binding.Register),
                                Word(Selected->ConstantCount * 4)}));
  return Error::success();
}

Error validateRootConstantBindings(const Module &M) {
  for (const Function &F : M)
    if (F.getMetadata(ExplicitBindingMD) && !getExplicitBinding(F))
      return createStringError(
          inconvertibleErrorCode(),
          "malformed explicit root-constant binding in '%s'",
          F.getName().str().c_str());
  return Error::success();
}

std::optional<RootConstantAccess> matchRootConstantAccess(Function &F) {
  if (F.isDeclaration())
    return std::nullopt;

  std::optional<ExplicitBinding> Binding = getExplicitBinding(F);
  if (F.getMetadata(ExplicitBindingMD) && !Binding)
    return std::nullopt;
  std::optional<RootConstantAccess> Access;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_handlefrombinding)
      continue;
    if (std::optional<RootConstantAccess> Matched =
            matchRootConstantHandle(*CI)) {
      if (Binding) {
        if (Matched->Space != Binding->Space ||
            Matched->Register != Binding->Register || Matched->RangeSize != 1)
          continue;
        Matched->RequiredSize = Binding->Size;
        Matched->ElementSize = std::min(Matched->ElementSize, Binding->Size);
      }
      if (Access)
        return std::nullopt; // More than one candidate: reject both.
      Access = std::move(Matched);
    }
  }
  if (!Access)
    return std::nullopt;

  std::optional<SmallVector<RootConstantRowLoad, 4>> Loads =
      collectRowLoads(*Access->Handle);
  if (!Loads)
    return std::nullopt;

  Access->Loads = std::move(*Loads);
  return Access;
}

uint32_t lowerRootConstantAccess(const RootConstantAccess &Access,
                                 Value *RootConstants,
                                 Value *RootConstantSize) {
  LLVMContext &Ctx = Access.Handle->getContext();
  Type *I64Ty = Type::getInt64Ty(Ctx);
  uint32_t RootConstantSizeNeeded =
      Access.RequiredSize.value_or(Access.ElementSize * Access.RangeSize);
  Value *Index = Access.Handle->getArgOperand(3);

  for (const RootConstantRowLoad &RL : Access.Loads) {
    auto *RetTy = cast<StructType>(RL.Load->getType());
    unsigned NumComponents = RetTy->getNumElements();
    Type *ElemTy = RetTy->getElementType(0);
    uint32_t ComponentSize = RowSizeBytes / NumComponents;

    IRBuilder<> Builder(RL.Load);
    Value *Index64 = Builder.CreateZExt(Index, I64Ty);
    Value *Size64 = Builder.CreateZExt(RootConstantSize, I64Ty);
    Value *IndexInBounds = Builder.CreateICmpULT(
        Index64, ConstantInt::get(I64Ty, Access.RangeSize));
    Value *ElementOffset =
        Builder.CreateMul(Index64, ConstantInt::get(I64Ty, Access.ElementSize),
                          "root_const.elem_off");
    Value *RowOffset = Builder.CreateMul(Builder.CreateZExt(RL.Row, I64Ty),
                                         ConstantInt::get(I64Ty, RowSizeBytes),
                                         "root_const.row_off");

    // A row may be only partly present, but cannot borrow bytes from another
    // array element. Wide arithmetic and an independent index check prevent
    // large invalid indices from wrapping into the supplied block.
    SmallVector<PHINode *, 4> Merged(NumComponents);
    for (unsigned Component = 0; Component != NumComponents; ++Component) {
      IRBuilder<> ComponentBuilder(RL.Load);
      Value *RelativeOffset = ComponentBuilder.CreateAdd(
          RowOffset, ConstantInt::get(I64Ty, Component * ComponentSize));
      Value *RelativeEnd = ComponentBuilder.CreateAdd(
          RelativeOffset, ConstantInt::get(I64Ty, ComponentSize));
      Value *ElementInBounds = ComponentBuilder.CreateICmpULE(
          RelativeEnd, ConstantInt::get(I64Ty, Access.ElementSize));
      Value *ComponentOffset =
          ComponentBuilder.CreateAdd(ElementOffset, RelativeOffset);
      Value *ComponentEnd = ComponentBuilder.CreateAdd(
          ComponentOffset, ConstantInt::get(I64Ty, ComponentSize));
      Value *SpanInBounds =
          ComponentBuilder.CreateICmpULE(ComponentEnd, Size64);
      Value *InBounds = ComponentBuilder.CreateAnd(
          IndexInBounds,
          ComponentBuilder.CreateAnd(ElementInBounds, SpanInBounds),
          "root_const.inbounds");
      Instruction *ThenTerm = nullptr;
      Instruction *ElseTerm = nullptr;
      SplitBlockAndInsertIfThenElse(InBounds, RL.Load, &ThenTerm, &ElseTerm);
      IRBuilder<> ThenBuilder(ThenTerm);
      IRBuilder<> MergeBuilder(RL.Load);
      Value *Ptr =
          ThenBuilder.CreateInBoundsGEP(ThenBuilder.getInt8Ty(), RootConstants,
                                        ComponentOffset, "root_const.ptr");
      Align Alignment =
          commonAlignment(commonAlignment(Align(4), Access.ElementSize),
                          Component * ComponentSize);
      Value *Loaded = ThenBuilder.CreateAlignedLoad(ElemTy, Ptr, Alignment,
                                                    "root_const.load");
      Merged[Component] = MergeBuilder.CreatePHI(ElemTy, 2, "root_const.value");
      Merged[Component]->addIncoming(Loaded, ThenTerm->getParent());
      Merged[Component]->addIncoming(Constant::getNullValue(ElemTy),
                                     ElseTerm->getParent());
    }

    IRBuilder<> MergeBuilder(RL.Load);
    Value *Result = PoisonValue::get(RetTy);
    for (unsigned Component = 0; Component != NumComponents; ++Component)
      Result = MergeBuilder.CreateInsertValue(Result, Merged[Component],
                                              {Component});

    RL.Load->replaceAllUsesWith(Result);
    RL.Load->eraseFromParent();
  }

  Access.Handle->eraseFromParent();
  return RootConstantSizeNeeded;
}

PreservedAnalyses RootConstantLoweringPass::run(Module &M,
                                                ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (lowerFunctionRootConstants(F))
      Changed = true;

  // An unused `handlefrombinding` declaration is left behind once its last
  // caller is rewritten away; a candidate this pass declined to rewrite
  // (or left for `feme::cpu::ResourceLoweringPass`, see the header
  // comment) may still have users, and is left alone.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        F.getIntrinsicID() == Intrinsic::dx_resource_handlefrombinding)
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace feme::cpu
