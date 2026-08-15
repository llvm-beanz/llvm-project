//===- ResourceLowering.cpp - Lower raised resources to NVPTX -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/NVPTX/ResourceLowering.h"

#include "feme/Core/ShaderStage.h"

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
#include "llvm/IR/IntrinsicsSPIRV.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;
using namespace feme::nvptx;

namespace {

/// NVPTX's global address space (`llvm::NVPTXAS::ADDRESS_SPACE_GLOBAL`),
/// where a kernel's buffer arguments point -- the same numeric value as
/// AMDGPU's own equivalent (see feme::amdgpu::ResourceLoweringPass's
/// `GlobalAddressSpace`), purely by coincidence.
constexpr unsigned GlobalAddressSpace = 1;

/// Which of the two parallel raised resource-op families (see
/// feme::dxil::OpRaisingPass and feme::SPIRVToLLVMTranslator, "Per-Format
/// Representation Strategy" in feme/docs/Design.md) a binding is expressed
/// with. The families are parallel in what they let a shader do -- create a
/// handle, then read/write a typed buffer element through it -- but not in
/// how they spell the access itself: DX has a dedicated
/// load/store-typedbuffer intrinsic pair, where the load additionally
/// returns a `{value, checkbit}` pair; SPIR-V instead has a `getpointer`
/// intrinsic addressing an element, which ordinary `load`/`store`
/// instructions then go through (see feme::SPIRVToLLVMTranslator's
/// `ImageReadPattern`/`ImageWritePattern`, which spell a DXIL-raised typed
/// buffer access from the *other* direction the same way -- see
/// feme::spirv::RaisedLoweringPass). This pass therefore handles the two
/// shapes with separate code paths, selected by \p Family, rather than
/// forcing one shape's rewrite logic onto the other's ops.
enum class ResourceFamily { DX, SPIRV };

struct ResourceOps {
  ResourceFamily Family;
  Intrinsic::ID HandleFromBinding;
  StringRef HandleTypeName;
};

constexpr ResourceOps DXResourceOps = {ResourceFamily::DX,
                                       Intrinsic::dx_resource_handlefrombinding,
                                       "dx.TypedBuffer"};
constexpr ResourceOps SPIRVResourceOps = {
    ResourceFamily::SPIRV, Intrinsic::spv_resource_handlefrombinding,
    "spirv.Image"};
constexpr const ResourceOps *AllResourceOps[] = {&DXResourceOps,
                                                 &SPIRVResourceOps};

/// One resource binding an entry point uses, together with every
/// `...resource.handlefrombinding` call that materializes a handle for it.
/// Multiple calls are common: a shader that touches the same resource from
/// several places gets one handle each time.
struct Binding {
  const ResourceOps *Ops = nullptr;
  uint32_t Space = 0;
  uint32_t Register = 0;
  Type *ElementType = nullptr;
  SmallVector<CallInst *, 2> Handles;

  bool operator<(const Binding &Other) const {
    return std::tie(Space, Register) < std::tie(Other.Space, Other.Register);
  }
};

/// Returns the intrinsic ID of the call \p V is, or `not_intrinsic`.
Intrinsic::ID getIntrinsicID(const Value *V) {
  const auto *CI = dyn_cast<CallInst>(V);
  const Function *Callee = CI ? CI->getCalledFunction() : nullptr;
  return Callee ? Callee->getIntrinsicID() : Intrinsic::not_intrinsic;
}

/// Returns the resource op family whose `...handlefrombinding` intrinsic is
/// \p ID, or nullptr if \p ID isn't one of those.
const ResourceOps *getResourceOps(Intrinsic::ID ID) {
  for (const ResourceOps *Ops : AllResourceOps)
    if (Ops->HandleFromBinding == ID)
      return Ops;
  return nullptr;
}

/// Checks that every use of the handle \p HandleCI produces is a typed buffer
/// access this pass can rewrite.
///
/// A DX load's result must additionally only be consumed by `extractvalue`,
/// since the raised intrinsic returns a `{value, checkbit}` pair that has no
/// lowered counterpart of its own. A SPIR-V `getpointer` call's result must
/// be consumed by exactly one ordinary `load`, or by exactly one `store` it
/// is the pointer operand of -- anything else (multiple uses, a store that
/// uses it as the *stored* value, ...) is more than a single element access,
/// which this pass does not attempt to reason about.
bool hasOnlySupportedUses(const CallInst &HandleCI, const ResourceOps &Ops) {
  for (const User *U : HandleCI.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      return false;

    if (Ops.Family == ResourceFamily::DX) {
      switch (getIntrinsicID(CI)) {
      case Intrinsic::dx_resource_store_typedbuffer:
        // The handle must be the resource operand, not the stored value.
        if (CI->getArgOperand(0) != &HandleCI)
          return false;
        break;
      case Intrinsic::dx_resource_load_typedbuffer:
        for (const User *LoadUser : CI->users())
          if (!isa<ExtractValueInst>(LoadUser))
            return false;
        break;
      default:
        return false;
      }
      continue;
    }

    if (getIntrinsicID(CI) != Intrinsic::spv_resource_getpointer ||
        !CI->hasOneUse())
      return false;
    const User *AccessUser = *CI->user_begin();
    if (const auto *SI = dyn_cast<StoreInst>(AccessUser)) {
      if (SI->getPointerOperand() != CI)
        return false;
    } else if (!isa<LoadInst>(AccessUser)) {
      return false;
    }
  }
  return true;
}

/// Returns the element type \p HandleCI's typed buffer accesses operate on.
/// DX's `target("dx.TypedBuffer", ElemTy, ...)` handle type spells this
/// directly as a type parameter. SPIR-V's `target("spirv.Image", ...)`
/// handle type does not -- its parameters describe the underlying image's
/// dimensionality/sampled type, not the (possibly vector) type a particular
/// access loads or stores -- so it is instead read off the load/store
/// through the first `getpointer` call found. Returns nullptr if \p HandleCI
/// has no accesses to read it from.
Type *getElementType(const CallInst &HandleCI, const ResourceOps &Ops) {
  if (Ops.Family == ResourceFamily::DX)
    return cast<TargetExtType>(HandleCI.getType())->getTypeParameter(0);

  for (const User *U : HandleCI.users()) {
    const auto *GetPointer = cast<CallInst>(U);
    const User *AccessUser = *GetPointer->user_begin();
    if (const auto *LI = dyn_cast<LoadInst>(AccessUser))
      return LI->getType();
    if (const auto *SI = dyn_cast<StoreInst>(AccessUser))
      return SI->getValueOperand()->getType();
  }
  return nullptr;
}

/// Collects the resource bindings \p F uses, or `std::nullopt` if any of them
/// is one this pass cannot model (see ResourceLoweringPass's class comment).
/// Bindings are returned in a deterministic (space, register) order, which is
/// the order they become kernel arguments in.
std::optional<SmallVector<Binding, 4>> collectBindings(Function &F) {
  SmallVector<Binding, 4> Bindings;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI)
      continue;
    const ResourceOps *Ops = getResourceOps(getIntrinsicID(CI));
    if (!Ops)
      continue;

    auto *HandleTy = dyn_cast<TargetExtType>(CI->getType());
    if (!HandleTy || HandleTy->getName() != Ops->HandleTypeName)
      return std::nullopt;

    auto *Space = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    auto *LowerBound = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    auto *Index = dyn_cast<ConstantInt>(CI->getArgOperand(3));
    if (!Space || !LowerBound || !Index)
      return std::nullopt;
    if (!hasOnlySupportedUses(*CI, *Ops))
      return std::nullopt;

    Type *ElementType = getElementType(*CI, *Ops);
    if (!ElementType)
      return std::nullopt;

    uint32_t Register = static_cast<uint32_t>(LowerBound->getZExtValue() +
                                              Index->getZExtValue());
    Binding *Existing = llvm::find_if(Bindings, [&](const Binding &B) {
      return B.Space == Space->getZExtValue() && B.Register == Register;
    });
    if (Existing != Bindings.end()) {
      // The same binding reached through two different element types would
      // need two differently-typed pointers for one resource.
      if (Existing->ElementType != ElementType || Existing->Ops != Ops)
        return std::nullopt;
      Existing->Handles.push_back(CI);
      continue;
    }

    Binding NewBinding;
    NewBinding.Ops = Ops;
    NewBinding.Space = static_cast<uint32_t>(Space->getZExtValue());
    NewBinding.Register = Register;
    NewBinding.ElementType = ElementType;
    NewBinding.Handles.push_back(CI);
    Bindings.push_back(std::move(NewBinding));
  }

  llvm::sort(Bindings);
  return Bindings;
}

/// Builds \p F's replacement: the same function with one trailing
/// `ptr addrspace(1)` parameter per entry of \p Bindings. The body is moved
/// across rather than cloned, so every instruction (including the handle
/// calls recorded in \p Bindings) stays valid and belongs to the new
/// function afterwards.
Function *addBindingArguments(Function &F, ArrayRef<Binding> Bindings) {
  SmallVector<Type *, 8> ParamTypes(F.getFunctionType()->params());
  Type *PtrTy = PointerType::get(F.getContext(), GlobalAddressSpace);
  ParamTypes.append(Bindings.size(), PtrTy);

  FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                          F.getFunctionType()->isVarArg());
  Function *NewF = Function::Create(NewTy, F.getLinkage(), F.getAddressSpace(),
                                    "", F.getParent());
  NewF->copyAttributesFrom(&F);
  NewF->setComdat(F.getComdat());
  NewF->splice(NewF->begin(), &F);

  for (auto [OldArg, NewArg] : llvm::zip(F.args(), NewF->args())) {
    NewArg.takeName(&OldArg);
    OldArg.replaceAllUsesWith(&NewArg);
  }

  unsigned ArgIndex = F.arg_size();
  for (const Binding &B : Bindings) {
    Argument &Arg = *(NewF->arg_begin() + ArgIndex++);
    SmallString<32> Name;
    raw_svector_ostream(Name) << "res.space" << B.Space << ".reg" << B.Register;
    Arg.setName(Name);
  }

  NewF->takeName(&F);
  F.replaceAllUsesWith(NewF);
  F.eraseFromParent();
  return NewF;
}

/// Rewrites a DX `load`/`store`-typedbuffer call \p Access -- the resource
/// operand of both is always argument 0, the element index argument 1, and
/// the stored value (for a store) argument 2 -- into an ordinary, aligned
/// load/store of \p Ptr.
void lowerDXAccess(CallInst &Access, Value *Ptr, Type *ElementType,
                   Align Alignment) {
  IRBuilder<> Builder(&Access);
  Value *Elem = Builder.CreateGEP(ElementType, Ptr, Access.getArgOperand(1));

  if (getIntrinsicID(&Access) == Intrinsic::dx_resource_store_typedbuffer) {
    Builder.CreateAlignedStore(Access.getArgOperand(2), Elem, Alignment);
    Access.eraseFromParent();
    return;
  }

  Value *Loaded = Builder.CreateAlignedLoad(ElementType, Elem, Alignment);
  // Field 0 is the loaded value; field 1 is the "checkbit" reporting whether
  // the access was in bounds, which plain memory always is.
  for (User *LoadUser : llvm::make_early_inc_range(Access.users())) {
    auto *EV = cast<ExtractValueInst>(LoadUser);
    Value *Replacement =
        EV->getIndices()[0] == 0
            ? Loaded
            : cast<Value>(ConstantInt::getTrue(EV->getContext()));
    EV->replaceAllUsesWith(Replacement);
    EV->eraseFromParent();
  }
  Access.eraseFromParent();
}

/// Rewrites a SPIR-V `llvm.spv.resource.getpointer` call \p Access into the
/// GEP'd element pointer it addresses within \p Ptr, and points the single
/// `load`/`store` already reading or writing through it (see
/// `hasOnlySupportedUses`) at that pointer directly -- it needs no other
/// changes, since it only cares about the pointer value, not which address
/// space computed it. (A plain `replaceAllUsesWith` cannot do this instead:
/// \p Ptr's address space differs from \p Access's generic one, and LLVM's
/// opaque pointer types encode address space, so the two are different
/// types.)
void lowerSPIRVAccess(CallInst &Access, Value *Ptr, Type *ElementType) {
  IRBuilder<> Builder(&Access);
  Value *Elem = Builder.CreateGEP(ElementType, Ptr, Access.getArgOperand(1));

  User *AccessUser = *Access.user_begin();
  if (auto *LI = dyn_cast<LoadInst>(AccessUser))
    LI->setOperand(LoadInst::getPointerOperandIndex(), Elem);
  else
    cast<StoreInst>(AccessUser)
        ->setOperand(StoreInst::getPointerOperandIndex(), Elem);
  Access.eraseFromParent();
}

/// Rewrites every typed buffer access through \p Handle into ordinary
/// loads/stores of \p Ptr.
void lowerHandleAccesses(CallInst &Handle, const ResourceOps &Ops, Value *Ptr,
                         Type *ElementType) {
  const DataLayout &DL = Handle.getModule()->getDataLayout();
  Align Alignment = DL.getABITypeAlign(ElementType);

  for (User *U : llvm::make_early_inc_range(Handle.users())) {
    auto *Access = cast<CallInst>(U);
    if (Ops.Family == ResourceFamily::DX)
      lowerDXAccess(*Access, Ptr, ElementType, Alignment);
    else
      lowerSPIRVAccess(*Access, Ptr, ElementType);
  }
  Handle.eraseFromParent();
}

/// Lowers every resource binding \p F uses, returning the rewritten function
/// (a new one, since its signature grows), or nullptr if \p F has no
/// bindings or uses one this pass cannot model.
Function *lowerFunctionResources(Function &F) {
  if (F.isDeclaration() || !feme::isShaderEntryPoint(F))
    return nullptr;

  std::optional<SmallVector<Binding, 4>> Bindings = collectBindings(F);
  if (!Bindings || Bindings->empty())
    return nullptr;

  Function *NewF = addBindingArguments(F, *Bindings);

  unsigned ArgIndex = NewF->arg_size() - Bindings->size();
  for (const Binding &B : *Bindings) {
    Value *Ptr = NewF->arg_begin() + ArgIndex++;
    for (CallInst *Handle : B.Handles)
      lowerHandleAccesses(*Handle, *B.Ops, Ptr, B.ElementType);
  }
  return NewF;
}

} // namespace

PreservedAnalyses ResourceLoweringPass::run(Module &M,
                                            ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    Changed |= lowerFunctionResources(F) != nullptr;

  // The handle-creation intrinsic's declaration is left behind once its last
  // caller is gone; NVPTX cannot select a call to it, so an unused
  // declaration surviving here would be a silent landmine.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        (F.getName().starts_with("llvm.dx.resource.") ||
         F.getName().starts_with("llvm.spv.resource.")))
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
