//===- ResourceLowering.cpp - Lower raised resources to AMDGPU -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/AMDGPU/ResourceLowering.h"

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
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;
using namespace feme::amdgpu;

namespace {

/// AMDGPU's global address space, which is where a kernel's buffer arguments
/// point.
constexpr unsigned GlobalAddressSpace = 1;

/// One resource binding an entry point uses, together with every
/// `llvm.dx.resource.handlefrombinding` call that materializes a handle for
/// it. Multiple calls are common: a shader that touches the same resource
/// from several places gets one handle each time.
struct Binding {
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

/// Checks that every use of the handle \p HandleCI produces is a typed buffer
/// access this pass can rewrite. A load's result must additionally only be
/// consumed by `extractvalue`, since the raised intrinsic returns a
/// {value, checkbit} pair that has no lowered counterpart of its own.
bool hasOnlySupportedUses(const CallInst &HandleCI) {
  for (const User *U : HandleCI.users()) {
    const auto *CI = dyn_cast<CallInst>(U);
    if (!CI)
      return false;
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
  }
  return true;
}

/// Collects the resource bindings \p F uses, or `std::nullopt` if any of them
/// is one this pass cannot model (see ResourceLoweringPass's class comment).
/// Bindings are returned in a deterministic (space, register) order, which is
/// the order they become kernel arguments in.
std::optional<SmallVector<Binding, 4>> collectBindings(Function &F) {
  SmallVector<Binding, 4> Bindings;
  for (Instruction &I : instructions(F)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (!CI || getIntrinsicID(CI) != Intrinsic::dx_resource_handlefrombinding)
      continue;

    auto *HandleTy = dyn_cast<TargetExtType>(CI->getType());
    if (!HandleTy || HandleTy->getName() != "dx.TypedBuffer")
      return std::nullopt;

    auto *Space = dyn_cast<ConstantInt>(CI->getArgOperand(0));
    auto *LowerBound = dyn_cast<ConstantInt>(CI->getArgOperand(1));
    auto *Index = dyn_cast<ConstantInt>(CI->getArgOperand(3));
    if (!Space || !LowerBound || !Index)
      return std::nullopt;
    if (!hasOnlySupportedUses(*CI))
      return std::nullopt;

    uint32_t Register = static_cast<uint32_t>(LowerBound->getZExtValue() +
                                              Index->getZExtValue());
    Binding *Existing = llvm::find_if(Bindings, [&](const Binding &B) {
      return B.Space == Space->getZExtValue() && B.Register == Register;
    });
    if (Existing != Bindings.end()) {
      // The same binding reached through two different element types would
      // need two differently-typed pointers for one resource.
      if (Existing->ElementType != HandleTy->getTypeParameter(0))
        return std::nullopt;
      Existing->Handles.push_back(CI);
      continue;
    }

    Binding NewBinding;
    NewBinding.Space = static_cast<uint32_t>(Space->getZExtValue());
    NewBinding.Register = Register;
    NewBinding.ElementType = HandleTy->getTypeParameter(0);
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

/// Rewrites the typed buffer accesses through \p Handle into ordinary
/// loads/stores of \p Ptr, indexed by the access's element index.
void lowerHandleAccesses(CallInst &Handle, Value *Ptr, Type *ElementType) {
  const DataLayout &DL = Handle.getModule()->getDataLayout();
  Align Alignment = DL.getABITypeAlign(ElementType);

  for (User *U : llvm::make_early_inc_range(Handle.users())) {
    auto *Access = cast<CallInst>(U);
    IRBuilder<> Builder(Access);

    if (getIntrinsicID(Access) == Intrinsic::dx_resource_store_typedbuffer) {
      Value *Elem =
          Builder.CreateGEP(ElementType, Ptr, Access->getArgOperand(1));
      Builder.CreateAlignedStore(Access->getArgOperand(2), Elem, Alignment);
      Access->eraseFromParent();
      continue;
    }

    Value *Elem = Builder.CreateGEP(ElementType, Ptr, Access->getArgOperand(1));
    Value *Loaded = Builder.CreateAlignedLoad(ElementType, Elem, Alignment);
    for (User *LoadUser : llvm::make_early_inc_range(Access->users())) {
      auto *EV = cast<ExtractValueInst>(LoadUser);
      // Field 0 is the loaded value; field 1 is the "checkbit" reporting
      // whether the access was in bounds, which plain memory always is.
      Value *Replacement =
          EV->getIndices()[0] == 0
              ? Loaded
              : cast<Value>(ConstantInt::getTrue(EV->getContext()));
      EV->replaceAllUsesWith(Replacement);
      EV->eraseFromParent();
    }
    Access->eraseFromParent();
  }
  Handle.eraseFromParent();
}

/// Lowers every resource binding \p F uses, returning the rewritten function
/// (a new one, since its signature grows), or nullptr if \p F has no
/// bindings or uses one this pass cannot model.
Function *lowerFunctionResources(Function &F) {
  if (F.isDeclaration() || !F.hasFnAttribute("hlsl.shader"))
    return nullptr;

  std::optional<SmallVector<Binding, 4>> Bindings = collectBindings(F);
  if (!Bindings || Bindings->empty())
    return nullptr;

  Function *NewF = addBindingArguments(F, *Bindings);

  unsigned ArgIndex = NewF->arg_size() - Bindings->size();
  for (const Binding &B : *Bindings) {
    Value *Ptr = NewF->arg_begin() + ArgIndex++;
    for (CallInst *Handle : B.Handles)
      lowerHandleAccesses(*Handle, Ptr, B.ElementType);
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
  // caller is gone; AMDGPU cannot select a call to it, so an unused
  // declaration surviving here would be a silent landmine.
  for (Function &F : llvm::make_early_inc_range(M.functions()))
    if (F.isDeclaration() && F.use_empty() &&
        F.getName().starts_with("llvm.dx.resource."))
      F.eraseFromParent();

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
