//===-- IR/Statepoint.cpp -- gc.statepoint utilities ---  -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// 
//===----------------------------------------------------------------------===//

#include "dxil/IR/Function.h"
#include "dxil/IR/Constant.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/Statepoint.h"

using namespace llvm;

namespace dxil {

bool isStatepoint(const ImmutableCallSite &CS) {
  if (!CS.getInstruction()) {
    // This is not a call site
    return false;
  }

  const Function *F = CS.getCalledFunction();
  return (F && F->getIntrinsicID() == Intrinsic::experimental_gc_statepoint);
}
bool isStatepoint(const Value *inst) {
  if (isa<InvokeInst>(inst) || isa<CallInst>(inst)) {
    ImmutableCallSite CS(inst);
    return isStatepoint(CS);
  }
  return false;
}
bool isStatepoint(const Value &inst) { return isStatepoint(&inst); }

bool isGCRelocate(const ImmutableCallSite &CS) {
  if (!CS.getInstruction()) {
    // This is not a call site
    return false;
  }

  return isGCRelocate(CS.getInstruction());
}
bool isGCRelocate(const Value *inst) {
  if (const CallInst *call = dyn_cast<CallInst>(inst)) {
    if (const Function *F = call->getCalledFunction()) {
      return F->getIntrinsicID() == Intrinsic::experimental_gc_relocate;
    }
  }
  return false;
}

bool isGCResult(const ImmutableCallSite &CS) {
  if (!CS.getInstruction()) {
    // This is not a call site
    return false;
  }

  return isGCResult(CS.getInstruction());
}
bool isGCResult(const Value *inst) {
  if (const CallInst *call = dyn_cast<CallInst>(inst)) {
    if (Function *F = call->getCalledFunction()) {
      return (F->getIntrinsicID() == Intrinsic::experimental_gc_result_int ||
              F->getIntrinsicID() == Intrinsic::experimental_gc_result_float ||
              F->getIntrinsicID() == Intrinsic::experimental_gc_result_ptr ||
              F->getIntrinsicID() == Intrinsic::experimental_gc_result);
    }
  }
  return false;
}

} // namespace dxil
