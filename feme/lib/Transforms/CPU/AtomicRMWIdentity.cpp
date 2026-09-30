//===- AtomicRMWIdentity.cpp - Maskable atomicrmw identity element -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// See AtomicRMWIdentity.h for this function's scope.
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/AtomicRMWIdentity.h"
#include "llvm/IR/Constants.h"

using namespace llvm;

std::optional<Constant *>
feme::cpu::getAtomicRMWIdentity(AtomicRMWInst::BinOp Op, Type *Ty) {
  switch (Op) {
  case AtomicRMWInst::Add:
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor:
  case AtomicRMWInst::UMax:
  case AtomicRMWInst::USubCond:
  case AtomicRMWInst::USubSat:
    return Constant::getNullValue(Ty);
  case AtomicRMWInst::And:
  case AtomicRMWInst::UMin:
    return Constant::getAllOnesValue(Ty);
  case AtomicRMWInst::Max:
    return ConstantInt::get(Ty,
                            APInt::getSignedMinValue(Ty->getIntegerBitWidth()));
  case AtomicRMWInst::Min:
    return ConstantInt::get(Ty,
                            APInt::getSignedMaxValue(Ty->getIntegerBitWidth()));
  case AtomicRMWInst::FAdd:
  case AtomicRMWInst::FSub:
    return ConstantFP::get(Ty, 0.0);
  case AtomicRMWInst::FMax:
  case AtomicRMWInst::FMaximum:
  case AtomicRMWInst::FMaximumNum:
    return ConstantFP::getInfinity(Ty, /*Negative=*/true);
  case AtomicRMWInst::FMin:
  case AtomicRMWInst::FMinimum:
  case AtomicRMWInst::FMinimumNum:
    return ConstantFP::getInfinity(Ty, /*Negative=*/false);
  case AtomicRMWInst::Xchg:
  case AtomicRMWInst::Nand:
  case AtomicRMWInst::UIncWrap:
  case AtomicRMWInst::UDecWrap:
  case AtomicRMWInst::BAD_BINOP:
    return std::nullopt;
  }
  llvm_unreachable("unhandled AtomicRMWInst::BinOp");
}
