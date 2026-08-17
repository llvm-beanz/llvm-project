//===- HullPhase.cpp - Hull control-point vs. patch-constant phase -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HullPhase.h"

#include "feme/Core/Signature.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

using namespace llvm;
using namespace feme;

namespace feme::cpu {

bool isPatchConstantPhase(const Function &F) {
  std::optional<EntrySignature> Sig = feme::dxil::getEntrySignature(F);
  if (!Sig)
    return false;
  for (const SignatureElement &Elt : Sig->Elements)
    if (Elt.Direction == SignatureDirection::PatchOutput)
      return true;
  return false;
}

} // namespace feme::cpu
