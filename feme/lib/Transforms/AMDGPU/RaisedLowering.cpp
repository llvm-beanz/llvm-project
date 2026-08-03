//===- RaisedLowering.cpp - Lower raised IR to AMDGPU conventions --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/AMDGPU/RaisedLowering.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::amdgpu;

namespace {

/// A raised, format-agnostic intrinsic (see feme::dxil::OpRaisingPass) that
/// queries a thread/group index by a constant component operand (0/1/2 for
/// x/y/z), and the three AMDGPU intrinsics -- one per component -- it maps
/// to. `llvm.dx.flattened.thread.id.in.group` is intentionally not in this
/// table: it takes no component operand (it is already a single linearized
/// index), so lowering it needs the group's dimensions to reconstruct that
/// linearization from AMDGPU's per-component `workitem.id`, which this pass
/// does not yet do (see this pass's header comment).
struct ComponentQuery {
  Intrinsic::ID RaisedID;
  std::array<Intrinsic::ID, 3> AMDGPUComponentIDs;
};

static const ComponentQuery ComponentQueries[] = {
    // ThreadId: the global/dispatch invocation's per-component index within
    // the whole dispatch grid, i.e. the workitem's index within its
    // workgroup ("thread id in group") is exactly what AMDGPU's
    // `llvm.amdgcn.workitem.id.*` reads -- FeMe does not need ThreadId's
    // dispatch-wide component here since that would additionally require
    // combining it with the workgroup id and workgroup size, which belongs
    // to `llvm.dx.thread.id`'s different, dispatch-relative semantics.
    {Intrinsic::dx_group_id,
     {Intrinsic::amdgcn_workgroup_id_x, Intrinsic::amdgcn_workgroup_id_y,
      Intrinsic::amdgcn_workgroup_id_z}},
    {Intrinsic::dx_thread_id_in_group,
     {Intrinsic::amdgcn_workitem_id_x, Intrinsic::amdgcn_workitem_id_y,
      Intrinsic::amdgcn_workitem_id_z}},
};

/// Rewrites a single call to one of the intrinsics in \p ComponentQueries
/// into the corresponding per-component AMDGPU intrinsic call, if \p CI's
/// sole operand is a constant in range [0, 3). Returns false (leaving \p CI
/// untouched) otherwise, so callers can safely skip calls whose component
/// isn't a compile-time constant (which this direct 1:1 mapping cannot
/// express) rather than crashing on them.
bool lowerComponentQuery(CallInst &CI, const ComponentQuery &Query) {
  if (CI.arg_size() != 1)
    return false;

  auto *Component = dyn_cast<ConstantInt>(CI.getArgOperand(0));
  if (!Component)
    return false;

  uint64_t ComponentIndex = Component->getZExtValue();
  if (ComponentIndex >= Query.AMDGPUComponentIDs.size())
    return false;

  Function *AMDGPUFn = Intrinsic::getOrInsertDeclaration(
      CI.getModule(), Query.AMDGPUComponentIDs[ComponentIndex]);
  IRBuilder<> Builder(&CI);
  CallInst *NewCall = Builder.CreateCall(AMDGPUFn, {}, CI.getName());
  CI.replaceAllUsesWith(NewCall);
  CI.eraseFromParent();
  return true;
}

} // namespace

PreservedAnalyses RaisedLoweringPass::run(Module &M, ModuleAnalysisManager &) {
  bool Changed = false;
  for (const ComponentQuery &Query : ComponentQueries) {
    Function *RaisedFn = M.getFunction(Intrinsic::getName(Query.RaisedID));
    if (!RaisedFn)
      continue;

    for (User *U : llvm::make_early_inc_range(RaisedFn->users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getCalledFunction() != RaisedFn)
        continue;
      Changed |= lowerComponentQuery(*CI, Query);
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
