//===--- DebugInfo.cpp - Debug Information Helper Classes -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the helper classes used to build and interpret debug
// information in LLVM IR form.
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/DebugInfo.h"
#include "LLVMContextImpl.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/DerivedTypes.h"
#include "dxil/IR/GVMaterializer.h"
#include "dxil/IR/Instructions.h"
#include "dxil/IR/IntrinsicInst.h"
#include "dxil/IR/Intrinsics.h"
#include "dxil/IR/Module.h"
#include "dxil/IR/ValueHandle.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;
using namespace llvm::dwarf;

namespace dxil {

DISubprogram *getDISubprogram(const MDNode *Scope) {
  if (auto *LocalScope = dyn_cast_or_null<DILocalScope>(Scope))
    return LocalScope->getSubprogram();
  return nullptr;
}

DISubprogram *getDISubprogram(const Function *F) {
  // We look for the first instr that has a debug annotation leading back to F.
  for (auto &BB : *F) {
    auto Inst = std::find_if(BB.begin(), BB.end(), [](const Instruction &Inst) {
      return Inst.getDebugLoc();
    });
    if (Inst == BB.end())
      continue;
    DebugLoc DLoc = Inst->getDebugLoc();
    const MDNode *Scope = DLoc.getInlinedAtScope();
    auto *Subprogram = getDISubprogram(Scope);
    return Subprogram->describes(F) ? Subprogram : nullptr;
  }

  return nullptr;
}

DICompositeTypeBase *getDICompositeType(DIType *T) {
  if (auto *C = dyn_cast_or_null<DICompositeTypeBase>(T))
    return C;

  if (auto *D = dyn_cast_or_null<DIDerivedTypeBase>(T)) {
    // This function is currently used by dragonegg and dragonegg does
    // not generate identifier for types, so using an empty map to resolve
    // DerivedFrom should be fine.
    DITypeIdentifierMap EmptyMap;
    return getDICompositeType(D->getBaseType().resolve(EmptyMap));
  }

  return nullptr;
}

DITypeIdentifierMap generateDITypeIdentifierMap(const NamedMDNode *CU_Nodes) {
  DITypeIdentifierMap Map;
  for (unsigned CUi = 0, CUe = CU_Nodes->getNumOperands(); CUi != CUe; ++CUi) {
    auto *CU = cast<DICompileUnit>(CU_Nodes->getOperand(CUi));
    DINodeArray Retain = CU->getRetainedTypes();
    for (unsigned Ti = 0, Te = Retain.size(); Ti != Te; ++Ti) {
      if (!isa<DICompositeType>(Retain[Ti]))
        continue;
      auto *Ty = cast<DICompositeType>(Retain[Ti]);
      if (MDString *TypeId = Ty->getRawIdentifier()) {
        // Definition has priority over declaration.
        // Try to insert (TypeId, Ty) to Map.
        std::pair<DITypeIdentifierMap::iterator, bool> P =
            Map.insert(std::make_pair(TypeId, Ty));
        // If TypeId already exists in Map and this is a definition, replace
        // whatever we had (declaration or definition) with the definition.
        if (!P.second && !Ty->isForwardDecl())
          P.first->second = Ty;
      }
    }
  }
  return Map;
}

bool stripDebugInfo(Function &F) {
  bool Changed = false;
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      if (I.getDebugLoc()) {
        Changed = true;
        I.setDebugLoc(DebugLoc());
      }
    }
  }
  return Changed;
}

bool StripDebugInfo(Module &M) {
  bool Changed = false;

  // Remove all of the calls to the debugger intrinsics, and remove them from
  // the module.
  if (Function *Declare = M.getFunction("llvm.dbg.declare")) {
    while (!Declare->use_empty()) {
      CallInst *CI = cast<CallInst>(Declare->user_back());
      CI->eraseFromParent();
    }
    Declare->eraseFromParent();
    Changed = true;
  }

  if (Function *DbgVal = M.getFunction("llvm.dbg.value")) {
    while (!DbgVal->use_empty()) {
      CallInst *CI = cast<CallInst>(DbgVal->user_back());
      CI->eraseFromParent();
    }
    DbgVal->eraseFromParent();
    Changed = true;
  }

  for (Module::named_metadata_iterator NMI = M.named_metadata_begin(),
         NME = M.named_metadata_end(); NMI != NME;) {
    NamedMDNode *NMD = &*NMI;
    ++NMI;
    if (NMD->getName().starts_with("llvm.dbg.")) {
      NMD->eraseFromParent();
      Changed = true;
    }
  }

  for (Function &F : M)
    Changed |= stripDebugInfo(F);

  if (GVMaterializer *Materializer = M.getMaterializer())
    Materializer->setStripDebugInfo();

  return Changed;
}

unsigned getDebugMetadataVersionFromModule(const Module &M) {
  if (auto *Val = mdconst::dyn_extract_or_null<ConstantInt>(
          M.getModuleFlag("Debug Info Version")))
    return Val->getZExtValue();
  return 0;
}

// HLSL Change - begin
bool hasDebugInfo(const Module &M) {
  // We might just get away with checking if there's "llvm.dbg.cu",
  // but this is more robust.
  for (Module::const_named_metadata_iterator NMI = M.named_metadata_begin(),
                                             NME = M.named_metadata_end();
       NMI != NME; ++NMI) {
    if (NMI->getName().starts_with("llvm.dbg.")) {
      return true;
    }
  }
  return false;
}
// HLSL Change - end

DenseMap<const Function *, DISubprogram *> makeSubprogramMap(const Module &M) {
  DenseMap<const Function *, DISubprogram *> R;

  NamedMDNode *CU_Nodes = M.getNamedMetadata("llvm.dbg.cu");
  if (!CU_Nodes)
    return R;

  for (MDNode *N : CU_Nodes->operands()) {
    auto *CUNode = cast<DICompileUnit>(N);
    for (auto *SP : CUNode->getSubprograms()) {
      if (Function *F = SP->getFunction())
        R.insert(std::make_pair(F, SP));
    }
  }
  return R;
}

} // namespace dxil
