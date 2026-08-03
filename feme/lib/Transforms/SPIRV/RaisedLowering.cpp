//===- RaisedLowering.cpp - Lower raised IR to SPIR-V conventions --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/SPIRV/RaisedLowering.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::spirv;

namespace {

/// The `Dim`/`Depth`/`Arrayed`/`MS` operands of a SPIR-V `OpTypeImage`
/// describing a buffer (i.e. a one-dimensional, non-arrayed, non-depth,
/// single-sampled image), which is how both HLSL's `Buffer`/`RWBuffer` and
/// DXIL's `TypedBuffer` resource kind map onto SPIR-V.
constexpr unsigned BufferDim = 5;
constexpr unsigned DepthUnknown = 2;
constexpr unsigned NotArrayed = 0;
constexpr unsigned SingleSampled = 0;
/// `Sampled`: 1 = read-only (an SRV), 2 = read-write (a UAV).
constexpr unsigned SampledReadOnly = 1;
constexpr unsigned SampledReadWrite = 2;

/// A raised, format-agnostic intrinsic and the `llvm.spv.*` intrinsic with
/// the same meaning. Both families are shaped identically (they are the two
/// backends' parallel lowerings of the same HLSL builtins), so lowering is a
/// straight substitution of the callee.
struct DirectMapping {
  Intrinsic::ID RaisedID;
  Intrinsic::ID SPIRVID;
};

static const DirectMapping DirectMappings[] = {
    {Intrinsic::dx_thread_id, Intrinsic::spv_thread_id},
    {Intrinsic::dx_group_id, Intrinsic::spv_group_id},
    {Intrinsic::dx_thread_id_in_group, Intrinsic::spv_thread_id_in_group},
    {Intrinsic::dx_flattened_thread_id_in_group,
     Intrinsic::spv_flattened_thread_id_in_group},
};

/// Returns the SPIR-V `ImageFormat` enumerator for a typed buffer of \p Width
/// components of type \p ScalarTy, or `std::nullopt` for a combination
/// SPIR-V has no storage image format for (notably three-component formats,
/// which SPIR-V does not define at all).
///
/// DXIL carries no image format of its own -- only the component type, with
/// the component *count* recovered from how the resource is accessed (see
/// feme::dxil::OpRaisingPass) -- so this reconstructs the narrowest format
/// that holds exactly those components. Naming the format precisely, rather
/// than emitting `Unknown`, keeps the result free of SPIR-V's
/// `StorageImageWriteWithoutFormat`/`StorageImageReadWithoutFormat`
/// capability requirements.
std::optional<unsigned> getImageFormat(Type *ScalarTy, unsigned Width,
                                       bool IsSigned) {
  // Indexed by component count 1/2/4 (index 2, a three-component format, is
  // unrepresentable and left as 0).
  static constexpr unsigned Float32[] = {3, 6, 0, 1}; // R32f, Rg32f, Rgba32f
  static constexpr unsigned Float16[] = {9, 7, 0, 2}; // R16f, Rg16f, Rgba16f
  static constexpr unsigned Signed32[] = {24, 25, 0, 21};
  static constexpr unsigned Signed16[] = {28, 26, 0, 22};
  static constexpr unsigned Unsigned32[] = {33, 35, 0, 30};
  static constexpr unsigned Unsigned16[] = {38, 36, 0, 31};

  if (Width == 0 || Width > 4)
    return std::nullopt;

  const unsigned *Formats = nullptr;
  if (ScalarTy->isFloatTy())
    Formats = Float32;
  else if (ScalarTy->isHalfTy())
    Formats = Float16;
  else if (ScalarTy->isIntegerTy(32))
    Formats = IsSigned ? Signed32 : Unsigned32;
  else if (ScalarTy->isIntegerTy(16))
    Formats = IsSigned ? Signed16 : Unsigned16;

  if (!Formats || Formats[Width - 1] == 0)
    return std::nullopt;
  return Formats[Width - 1];
}

/// Translates a DXIL typed buffer handle type into the SPIR-V image type
/// describing the same resource, or nullptr if it describes one SPIR-V has no
/// equivalent for.
TargetExtType *translateHandleType(TargetExtType *DXILTy) {
  if (DXILTy->getName() != "dx.TypedBuffer" ||
      DXILTy->getNumTypeParameters() != 1 || DXILTy->getNumIntParameters() != 3)
    return nullptr;

  Type *ElemTy = DXILTy->getTypeParameter(0);
  auto *VecTy = dyn_cast<FixedVectorType>(ElemTy);
  Type *ScalarTy = VecTy ? VecTy->getElementType() : ElemTy;
  unsigned Width = VecTy ? VecTy->getNumElements() : 1;
  bool IsUAV = DXILTy->getIntParameter(0) != 0;
  bool IsSigned = DXILTy->getIntParameter(2) != 0;

  std::optional<unsigned> Format = getImageFormat(ScalarTy, Width, IsSigned);
  if (!Format)
    return nullptr;

  // SPIR-V distinguishes signed from unsigned integer images by the handle
  // type's name rather than a parameter; float images are always plain
  // `spirv.Image`.
  bool UseSignedImage = IsSigned && ScalarTy->isIntegerTy();
  return TargetExtType::get(
      DXILTy->getContext(),
      UseSignedImage ? "spirv.SignedImage" : "spirv.Image", {ScalarTy},
      {BufferDim, DepthUnknown, NotArrayed, SingleSampled,
       IsUAV ? SampledReadWrite : SampledReadOnly, *Format});
}

/// Rewrites a call to \p From into the same call to \p To, which must take
/// the same operands and return the same type.
bool substituteCallee(CallInst &CI, Intrinsic::ID To) {
  Function *NewFn =
      Intrinsic::getOrInsertDeclaration(CI.getModule(), To, {CI.getType()});
  if (NewFn->getFunctionType() != CI.getFunctionType())
    return false;
  CI.setCalledFunction(NewFn);
  return true;
}

/// Returns the name string global to pass as a resource handle's name
/// operand. LLVM's SPIRV backend reads that operand's pointee string to name
/// the `OpVariable` it emits, so it must be a real global -- but DXIL only
/// stores resource names in its metadata, and strips them entirely in
/// release builds, so raising often has nothing to carry across. Synthesize a
/// binding-derived name in that case, which is both stable and unique.
Value *getOrCreateResourceName(IRBuilder<> &Builder, Value *Set, Value *Binding,
                               Value *Existing) {
  if (isa_and_nonnull<GlobalValue>(Existing))
    return Existing;

  auto *SetConst = dyn_cast<ConstantInt>(Set);
  auto *BindingConst = dyn_cast<ConstantInt>(Binding);
  SmallString<32> Name("resource");
  if (SetConst && BindingConst) {
    raw_svector_ostream OS(Name);
    OS << "_s" << SetConst->getZExtValue() << "_b"
       << BindingConst->getZExtValue();
  }
  return Builder.CreateGlobalString(Name, Name + ".str");
}

/// Rewrites the typed buffer accesses through \p Handle -- which has already
/// been replaced by the SPIR-V handle \p NewHandle -- into
/// `llvm.spv.resource.getpointer` plus an ordinary load or store, the form
/// LLVM's SPIRV backend selects `OpImageRead`/`OpImageWrite` from.
bool lowerHandleAccesses(CallInst &Handle, Value *NewHandle) {
  for (User *U : llvm::make_early_inc_range(Handle.users())) {
    auto *Access = dyn_cast<CallInst>(U);
    Function *Callee = Access ? Access->getCalledFunction() : nullptr;
    if (!Callee)
      return false;

    Intrinsic::ID ID = Callee->getIntrinsicID();
    bool IsStore = ID == Intrinsic::dx_resource_store_typedbuffer;
    if (!IsStore && ID != Intrinsic::dx_resource_load_typedbuffer)
      return false;

    IRBuilder<> Builder(Access);
    Function *GetPointer = Intrinsic::getOrInsertDeclaration(
        Handle.getModule(), Intrinsic::spv_resource_getpointer,
        {Builder.getPtrTy(), NewHandle->getType(),
         Access->getArgOperand(1)->getType()});
    Value *Ptr =
        Builder.CreateCall(GetPointer, {NewHandle, Access->getArgOperand(1)});

    if (IsStore) {
      Builder.CreateStore(Access->getArgOperand(2), Ptr);
      Access->eraseFromParent();
      continue;
    }

    auto *ResultTy = cast<StructType>(Access->getType());
    Value *Loaded = Builder.CreateLoad(ResultTy->getElementType(0), Ptr);
    for (User *LoadUser : llvm::make_early_inc_range(Access->users())) {
      auto *EV = dyn_cast<ExtractValueInst>(LoadUser);
      if (!EV)
        return false;
      // Field 0 is the loaded value; field 1 is the "checkbit" reporting
      // whether the access was in bounds, which SPIR-V has no equivalent of.
      Value *Replacement =
          EV->getIndices()[0] == 0 ? Loaded : cast<Value>(Builder.getTrue());
      EV->replaceAllUsesWith(Replacement);
      EV->eraseFromParent();
    }
    Access->eraseFromParent();
  }
  Handle.eraseFromParent();
  return true;
}

/// Lowers one `llvm.dx.resource.handlefrombinding` call, together with every
/// typed buffer access through it. DXIL's (register space, register) binding
/// is SPIR-V's (descriptor set, binding) pair, in the same operand order, so
/// only the handle type needs translating.
bool lowerResourceHandle(CallInst &CI) {
  auto *DXILTy = dyn_cast<TargetExtType>(CI.getType());
  TargetExtType *SPIRVTy = DXILTy ? translateHandleType(DXILTy) : nullptr;
  if (!SPIRVTy)
    return false;

  IRBuilder<> Builder(&CI);
  Function *NewFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Intrinsic::spv_resource_handlefrombinding, {SPIRVTy});
  SmallVector<Value *, 5> Args(CI.args());
  Args[4] = getOrCreateResourceName(Builder, Args[0], Args[1], Args[4]);
  Value *NewHandle = Builder.CreateCall(NewFn, Args);
  return lowerHandleAccesses(CI, NewHandle);
}

/// Runs \p Lower over every call to the intrinsic \p ID in \p M, erasing the
/// intrinsic's declaration once it has no callers left.
bool forEachIntrinsicCall(Module &M, Intrinsic::ID ID,
                          function_ref<bool(CallInst &)> Lower) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (F.getIntrinsicID() != ID)
      continue;
    for (User *U : llvm::make_early_inc_range(F.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getCalledFunction() != &F)
        continue;
      Changed |= Lower(*CI);
    }
    if (F.use_empty())
      F.eraseFromParent();
  }
  return Changed;
}

} // namespace

PreservedAnalyses RaisedLoweringPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;

  for (const DirectMapping &Mapping : DirectMappings)
    Changed |=
        forEachIntrinsicCall(M, Mapping.RaisedID, [&Mapping](CallInst &CI) {
          return substituteCallee(CI, Mapping.SPIRVID);
        });

  Changed |= forEachIntrinsicCall(M, Intrinsic::dx_resource_handlefrombinding,
                                  lowerResourceHandle);

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
