//===- SPIRVPushConstantLowering.cpp - SPIR-V push constant lowering ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/SPIRVPushConstantLowering.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;
using namespace feme::cpu;

namespace {

/// The LLVM address space LLVM's own SPIR-V backend uses for the
/// PushConstant storage class (see `storageClassToAddressSpace` in
/// `llvm/lib/Target/SPIRV/SPIRVUtils.h`), and the one
/// `feme::spirv::PushConstantGlobalVariablePattern` converts a push
/// constant `spirv.GlobalVariable` into.
constexpr unsigned PushConstantAddressSpace = 13;

/// Returns the module's push-constant global, or nullptr if it has none.
/// SPIR-V permits at most one, so the first one found is the only one.
GlobalVariable *findPushConstantGlobal(Module &M) {
  for (GlobalVariable &GV : M.globals())
    if (GV.getAddressSpace() == PushConstantAddressSpace)
      return &GV;
  return nullptr;
}

/// Whether \p F already has the `root_constants`/`root_constant_size`
/// parameter pair `feme::cpu::SPIRVResourceLoweringPass`'s own
/// `addResourceEnvParams` unconditionally appends to every function it
/// touches -- if so, that pass has already (or will already, since it runs
/// before this one in the pipeline) handled this function's push-constant
/// access itself, reusing those parameters, and this pass has nothing to
/// do (see the header comment's "combined case").
bool hasRootConstantParams(const Function &F) {
  return llvm::any_of(F.args(), [](const Argument &A) {
    return A.getName() == "root_constants";
  });
}

/// Whether every index of \p GEP is a compile-time constant (see the
/// header comment's scope note: a dynamically-indexed push-constant array
/// member is not recognized).
bool hasOnlyConstantIndices(const GEPOperator &GEP) {
  return llvm::all_of(GEP.indices(),
                      [](const Use &U) { return isa<ConstantInt>(U.get()); });
}

} // namespace

namespace feme::cpu {

std::optional<SPIRVPushConstantAccess>
matchSPIRVPushConstantAccess(Function &F) {
  if (F.isDeclaration())
    return std::nullopt;

  GlobalVariable *PC = findPushConstantGlobal(*F.getParent());
  if (!PC)
    return std::nullopt;

  SPIRVPushConstantAccess Access;
  Access.Global = PC;

  for (User *U : PC->users()) {
    // A constant-index GEP off the global's own address is almost always
    // folded straight to a `ConstantExpr`, not left as a real
    // `GetElementPtrInst`: the global's own address and every index are
    // already compile-time constants, and every ordinary constant-folding
    // IR builder (including the one `feme-translate`'s
    // `--llvmdialect-to-llvmir` step uses) collapses that eagerly. Handle
    // it identically to a genuine instruction-typed GEP below (both are a
    // `GEPOperator`), except a `ConstantExpr`'s own uses can span more than
    // one function (constants are uniqued module-wide, so two different
    // push-constant-consuming shaders reading the same struct offset share
    // the exact same `ConstantExpr` instance) -- unlike an
    // instruction-typed GEP, already guaranteed single-function by `UI`'s
    // own `getFunction()` filter above, each of *its* users needs its own
    // per-load function filter here instead. Missing this case left every
    // non-zero-offset load unrewritten (still referencing `@buffer`
    // itself, an external declaration with no definition or initializer),
    // producing a `JIT session error: Symbols not found: [ buffer ]` at
    // run time for any push-constant struct with more than one member at
    // a nonzero offset -- confirmed by reducing a real
    // `offload-test-suite` `Feature/PushConstant/bool.test` failure (the
    // first, offset-0 member loaded fine; the second, at offset 4, did
    // not) down to this exact constant-expression shape.
    if (auto *CE = dyn_cast<ConstantExpr>(U)) {
      if (CE->getOpcode() != Instruction::GetElementPtr ||
          !hasOnlyConstantIndices(cast<GEPOperator>(*CE)))
        return std::nullopt; // A GEP too dynamic to fold.
      for (User *GU : CE->users()) {
        auto *GLoad = dyn_cast<LoadInst>(GU);
        if (!GLoad || GLoad->getFunction() != &F)
          continue; // Not a load, or not this function's own use.
        Access.Loads.push_back(GLoad);
      }
      continue;
    }

    auto *UI = dyn_cast<Instruction>(U);
    if (!UI || UI->getFunction() != &F)
      continue; // Not this function's use; visited when that one is.

    if (auto *LI = dyn_cast<LoadInst>(UI)) {
      Access.Loads.push_back(LI);
      continue;
    }
    auto *GEP = dyn_cast<GetElementPtrInst>(UI);
    if (!GEP || !hasOnlyConstantIndices(cast<GEPOperator>(*GEP)))
      return std::nullopt; // A store, or a GEP too dynamic to fold.

    for (User *GU : GEP->users()) {
      auto *GLoad = dyn_cast<LoadInst>(GU);
      if (!GLoad)
        return std::nullopt; // A store, or a further GEP: unsupported.
      Access.Loads.push_back(GLoad);
    }
  }

  if (Access.Loads.empty())
    return std::nullopt; // The global exists, but not referenced by `F`.
  return Access;
}

uint32_t lowerSPIRVPushConstantAccess(const SPIRVPushConstantAccess &Access,
                                      Value *RootConstants,
                                      Value *RootConstantSize) {
  LLVMContext &Ctx = RootConstants->getContext();
  Type *I32Ty = Type::getInt32Ty(Ctx);
  Type *I64Ty = Type::getInt64Ty(Ctx);
  const DataLayout &DL = Access.Global->getDataLayout();

  // (Roadmap L10) The tightest root-constant span this access genuinely
  // needs: the highest byte any recognized load actually reads, not
  // `Access.Global`'s own declared-type `DataLayout` store size. Those two
  // differ whenever the CPU target's own struct layout pads the block's
  // tail wider than any real access reaches -- e.g. a trailing `int3`/
  // `float3` member's vector alignment rounds up to the next power of two
  // of its store size (12 -> 16) on a target whose data layout does not
  // mark vectors as element-aligned, inflating the *whole struct's* size
  // well past the last byte any `int3`/`float3` load actually touches.
  // Every access this pass recognizes has a compile-time-constant byte
  // offset (see the file comment's scope note: no dynamic index is ever
  // accepted here), so -- unlike `feme::cpu::RootConstantLowering.h`'s own
  // DXIL root constant, which must report its full declared size because a
  // dynamic row/array index means there is no longer a fixed set of bytes
  // to inspect statically -- there is no dynamic access this tighter span
  // could ever fail to cover.
  uint32_t MaxAccessedByte = 0;

  for (Instruction *LoadI : Access.Loads) {
    auto *Load = cast<LoadInst>(LoadI);
    Value *Ptr = Load->getPointerOperand();

    uint64_t ByteOffset = 0;
    if (Ptr != Access.Global) {
      APInt Offset(DL.getIndexSizeInBits(Ptr->getType()->getPointerAddressSpace()),
                  0);
      bool Resolved = cast<GEPOperator>(Ptr)->accumulateConstantOffset(
          DL, Offset); // Guaranteed by `hasOnlyConstantIndices`.
      (void)Resolved;
      assert(Resolved && "matchSPIRVPushConstantAccess only accepts "
                         "constant-index GEPs");
      ByteOffset = Offset.getZExtValue();
    }

    Type *LoadedTy = Load->getType();
    uint64_t LoadSize = DL.getTypeStoreSize(LoadedTy).getFixedValue();
    MaxAccessedByte =
        std::max<uint64_t>(MaxAccessedByte, ByteOffset + LoadSize);

    IRBuilder<> Builder(Load);
    Value *InBounds = Builder.CreateICmpULE(
        ConstantInt::get(I32Ty, ByteOffset + LoadSize), RootConstantSize,
        "push_const.inbounds");

    Instruction *ThenTerm = nullptr;
    Instruction *ElseTerm = nullptr;
    SplitBlockAndInsertIfThenElse(InBounds, Load, &ThenTerm, &ElseTerm);

    IRBuilder<> ThenBuilder(ThenTerm);
    Value *Offset64 = ConstantInt::get(I64Ty, ByteOffset);
    Value *ElemPtr = ThenBuilder.CreateInBoundsGEP(
        ThenBuilder.getInt8Ty(), RootConstants, Offset64, "push_const.ptr");
    Value *Loaded = ThenBuilder.CreateAlignedLoad(
        LoadedTy, ElemPtr, Load->getAlign(), "push_const.load");

    IRBuilder<> MergeBuilder(Load); // The original block is now the merge.
    PHINode *Merged = MergeBuilder.CreatePHI(LoadedTy, 2, "push_const.value");
    Merged->addIncoming(Loaded, ThenTerm->getParent());
    Merged->addIncoming(Constant::getNullValue(LoadedTy), ElseTerm->getParent());

    Load->replaceAllUsesWith(Merged);
    Load->eraseFromParent();
  }

  // Drop every now-unused `getelementptr` this access rewrote through; the
  // global itself is left for the caller to erase once every function
  // referencing it has been processed (see the pass's own `run`).
  for (User *U : llvm::make_early_inc_range(Access.Global->users()))
    if (auto *GEP = dyn_cast<GetElementPtrInst>(U); GEP && GEP->use_empty())
      GEP->eraseFromParent();

  return MaxAccessedByte;
}

PreservedAnalyses SPIRVPushConstantLoweringPass::run(Module &M,
                                                     ModuleAnalysisManager &) {
  bool Changed = false;
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (F.isDeclaration() || hasRootConstantParams(F))
      continue;
    std::optional<SPIRVPushConstantAccess> Access =
        matchSPIRVPushConstantAccess(F);
    if (!Access)
      continue;

    LLVMContext &Ctx = F.getContext();
    SmallVector<Type *, 2> ParamTypes(F.getFunctionType()->params());
    ParamTypes.push_back(PointerType::get(Ctx, 0));
    ParamTypes.push_back(Type::getInt32Ty(Ctx));
    FunctionType *NewTy = FunctionType::get(F.getReturnType(), ParamTypes,
                                            F.getFunctionType()->isVarArg());
    Function *NewF = Function::Create(NewTy, F.getLinkage(),
                                      F.getAddressSpace(), "", F.getParent());
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
    // is the first real case to reach this exact helper with a
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
    Value *RootConstants = &*ArgIt++;
    RootConstants->setName("root_constants");
    Value *RootConstantSize = &*ArgIt++;
    RootConstantSize->setName("root_constant_size");
    NewF->takeName(&F);
    F.replaceAllUsesWith(NewF);
    F.eraseFromParent();

    // Re-matched against `NewF`: `addRootConstantParams`'s equivalent above
    // rebuilds the function, and the loads `Access` collected belong to the
    // original `F`'s instructions -- which `splice` moved into `NewF`
    // unchanged (a `BasicBlock::splice` relocates instructions in place, it
    // does not clone them), so `Access.Loads`' pointers stay valid; only
    // `Access.Global` may need nothing further, since it is a module-level
    // `GlobalVariable`, not per-function.
    uint32_t RootConstantSizeNeeded =
        lowerSPIRVPushConstantAccess(*Access, RootConstants, RootConstantSize);

    Type *I32Ty = Type::getInt32Ty(Ctx);
    MDNode *Node = MDNode::get(
        Ctx,
        {MDString::get(Ctx, NewF->getName()),
         ConstantAsMetadata::get(ConstantInt::get(I32Ty, RootConstantSizeNeeded)),
         ConstantAsMetadata::get(ConstantInt::getFalse(Ctx)),
         ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0)),
         ConstantAsMetadata::get(ConstantInt::get(I32Ty, 0))});
    NewF->getParent()
        ->getOrInsertNamedMetadata("feme.cpu.resources")
        ->addOperand(Node);
    Changed = true;
  }

  if (Changed) {
    if (GlobalVariable *PC = findPushConstantGlobal(M); PC && PC->use_empty())
      PC->eraseFromParent();
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace feme::cpu
