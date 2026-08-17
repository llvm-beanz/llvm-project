//===- DomainInvocations.cpp - Tessellator-to-domain-batch marshaling ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/DomainInvocations.h"

#include "feme/Target/CPU/RuntimeABI.h"

using namespace feme::graphics;

std::vector<feme::cpu::FemeDomainInvocation>
feme::graphics::buildDomainInvocations(const TessellatedPatch &Patch) {
  std::vector<cpu::FemeDomainInvocation> Invocations;
  Invocations.reserve(Patch.Points.size());
  for (const DomainPoint &Point : Patch.Points) {
    cpu::FemeDomainInvocation Invocation{};
    Invocation.DomainLocation[0] = Point.U;
    Invocation.DomainLocation[1] = Point.V;
    Invocation.DomainLocation[2] = Point.W;
    Invocations.push_back(Invocation);
  }
  return Invocations;
}
