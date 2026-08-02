//===- OpRaising.cpp - Raise dx.op.* calls to idiomatic LLVM IR ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/DXIL/OpRaising.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace feme::dxil;

namespace {

/// A DXIL opcode (see `llvm/lib/Target/DirectX/DXIL.td`) that this pass
/// knows how to raise, and the single LLVM intrinsic call it was lowered
/// from. The opcode values below are DXIL's frozen wire-format encoding (the
/// numeric literal in each `DXILOp<N, ...>` tablegen definition), not
/// something FeMe controls or that changes across DXIL versions, so they are
/// safe to hard-code here rather than needing a dependency on the
/// DirectX-target-private generated tables that back `llvm::dxil::OpCode`.
struct RaisableOp {
  unsigned Opcode;
  Intrinsic::ID ID;
  /// Whether this intrinsic is overloaded on its (sole, after the opcode)
  /// operand's type, i.e. whether `getOrInsertDeclaration` needs that type
  /// passed as an explicit overload argument.
  bool Overloaded;
};

// clang-format off
static const RaisableOp UnaryOps[] = {
    // Scalar math raised to a standard LLVM intrinsic.
    {6, Intrinsic::fabs, true},              // Abs
    {7, Intrinsic::dx_saturate, true},       // Saturate
    {8, Intrinsic::dx_isnan, true},          // IsNan
    {9, Intrinsic::dx_isinf, true},          // IsInf
    {12, Intrinsic::cos, true},              // Cos
    {13, Intrinsic::sin, true},              // Sin
    {14, Intrinsic::tan, true},              // Tan
    {15, Intrinsic::acos, true},             // ACos
    {16, Intrinsic::asin, true},             // ASin
    {17, Intrinsic::atan, true},             // ATan
    {18, Intrinsic::cosh, true},             // HCos
    {19, Intrinsic::sinh, true},             // HSin
    {20, Intrinsic::tanh, true},             // HTan
    {21, Intrinsic::exp2, true},             // Exp2
    {22, Intrinsic::dx_frac, true},          // Frac
    {23, Intrinsic::log2, true},             // Log2
    {24, Intrinsic::sqrt, true},             // Sqrt
    {25, Intrinsic::dx_rsqrt, true},         // RSqrt
    {26, Intrinsic::roundeven, true},        // Round (round-to-nearest-even)
    {27, Intrinsic::floor, true},            // Floor
    {28, Intrinsic::ceil, true},             // Ceil
    {29, Intrinsic::trunc, true},            // Trunc
    {30, Intrinsic::bitreverse, true},       // Rbits
};

// Thread/wave queries: fixed i32 (or no) operands, never overloaded.
static const RaisableOp ThreadWaveOps[] = {
    {93, Intrinsic::dx_thread_id, false},                     // ThreadId
    {94, Intrinsic::dx_group_id, false},                      // GroupId
    {95, Intrinsic::dx_thread_id_in_group, false},             // ThreadIdInGroup
    {96, Intrinsic::dx_flattened_thread_id_in_group, false},   // FlattenedThreadIdInGroup
    {110, Intrinsic::dx_wave_is_first_lane, false},            // WaveIsFirstLane
    {111, Intrinsic::dx_wave_getlaneindex, false},             // WaveGetLaneIndex
};
// clang-format on

const RaisableOp *lookupRaisableOp(unsigned Opcode) {
  for (const RaisableOp &Op : UnaryOps)
    if (Op.Opcode == Opcode)
      return &Op;
  for (const RaisableOp &Op : ThreadWaveOps)
    if (Op.Opcode == Opcode)
      return &Op;
  return nullptr;
}

/// Rewrites a single `dx.op.*` call to the LLVM intrinsic call it was
/// lowered from, per \p RaiseAs. Returns false (leaving \p CI untouched) if
/// the call's shape doesn't match what's expected for \p RaiseAs (e.g. a
/// missing opcode operand), so callers can leave unrecognized shapes alone
/// rather than crashing on malformed/unexpected input.
bool raiseCall(CallInst &CI, const RaisableOp &RaiseAs) {
  // Operand 0 is always the opcode; the remaining operands (if any) are the
  // op's actual arguments, in order.
  if (CI.arg_size() == 0)
    return false;
  SmallVector<Value *, 2> Args(llvm::drop_begin(CI.args()));

  // The overload key is the (sole) operand's type, not necessarily the
  // call's result type: e.g. IsNan/IsInf take a float-family operand but
  // return i1, and it's the operand type that selects the intrinsic
  // overload (`llvm.dx.isnan.f32`, not `.i1`).
  Module &M = *CI.getModule();
  Function *IntrinFn = RaiseAs.Overloaded
                           ? Intrinsic::getOrInsertDeclaration(
                                 &M, RaiseAs.ID, {Args[0]->getType()})
                           : Intrinsic::getOrInsertDeclaration(&M, RaiseAs.ID);

  IRBuilder<> Builder(&CI);
  CallInst *NewCall = Builder.CreateCall(IntrinFn, Args, CI.getName());
  // Only floating-point operations carry fast-math flags; guard the copy so
  // this doesn't assert on the integer/predicate ops in RaisableOp (e.g.
  // ThreadId, IsNan's i1 result).
  if (isa<FPMathOperator>(NewCall) && isa<FPMathOperator>(CI))
    NewCall->copyFastMathFlags(&CI);
  CI.replaceAllUsesWith(NewCall);
  CI.eraseFromParent();
  return true;
}

} // namespace

PreservedAnalyses OpRaisingPass::run(Module &M, ModuleAnalysisManager &AM) {
  bool Changed = false;

  // Snapshot the function list: raising erases `dx.op.*` declarations once
  // they have no more callers, and inserts new intrinsic declarations, both
  // of which would invalidate an in-place iterator over `M.functions()`.
  for (Function &F : llvm::make_early_inc_range(M.functions())) {
    if (!F.isDeclaration() || !F.getName().starts_with("dx.op."))
      continue;

    for (User *U : llvm::make_early_inc_range(F.users())) {
      auto *CI = dyn_cast<CallInst>(U);
      if (!CI || CI->getCalledFunction() != &F)
        continue;

      auto *OpcodeConst = dyn_cast<ConstantInt>(CI->getArgOperand(0));
      if (!OpcodeConst)
        continue;

      const RaisableOp *RaiseAs = lookupRaisableOp(OpcodeConst->getZExtValue());
      if (!RaiseAs)
        continue;

      Changed |= raiseCall(*CI, *RaiseAs);
    }

    if (F.use_empty()) {
      F.eraseFromParent();
      Changed = true;
    }
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}
