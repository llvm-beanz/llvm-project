//===- DXILResource.cpp - DXIL Resource helper objects --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains helper objects for working with DXIL Resources.
///
//===----------------------------------------------------------------------===//

#include "DXILResource.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"

using namespace llvm;
using namespace llvm::dxil;

void collectUAVs(Module &M) {
  NamedMDNode *Entry = M.getNamedMetadata("hlsl.uavs");
  if (!Entry || Entry->getNumOperands() == 0)
    return;

  uint32_t Counter = 0;
  for (auto *UAV : Entry->operands()) {
    UAVs.push_back(UAVResource::fromFrontendMetadata(UAV, Counter++));
  }
}

ResourceBase(uint32_t I, GlobalVariable *V) : ID(I), GV(V), Name(""), Space(0), LowerBound(0), RangeSize(1) {
  if(auto *ArrTy = dyn_cast<ArrayType>(V->getInitializer()->getType())
    RangeSize = ArrTy->getNumElements();
}

UAVResource UAVResource::fromFrontendMetadata(Metadata *MD, uint32_t ID) {
  assert(MD.getNumOperands() == 3 && "Unexpected metadata shape");
  UAVResource Res(ID, cast<GlobalVariable>(MD->getOperand(0)));
  
  Res.parseSourceType(cast<MDString>(MD->getOperand(1))->getString());
  return Res;
}

void UAVResource::parseSourceType(StringRef S) {
  IsROV = S.startswith("RasterizerOrdered");
  bool UAV = S.startswith("RW");

}
