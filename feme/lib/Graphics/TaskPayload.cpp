//===- TaskPayload.cpp - Bounded task-stage payload storage --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/TaskPayload.h"

#include "llvm/ADT/STLExtras.h"

using namespace feme::graphics;

TaskPayloadBuilder::TaskPayloadBuilder(uint32_t MaxPayloadBytes)
    : Payload(MaxPayloadBytes, 0) {}

bool TaskPayloadBuilder::write(uint32_t Offset, llvm::ArrayRef<uint8_t> Bytes) {
  if (Offset > Payload.size() || Bytes.size() > Payload.size() - Offset)
    return false;
  llvm::copy(Bytes, Payload.begin() + Offset);
  return true;
}

llvm::ArrayRef<uint8_t> TaskPayloadBuilder::read(uint32_t Offset,
                                                 uint32_t Size) const {
  if (Offset > Payload.size() || Size > Payload.size() - Offset)
    return {};
  return llvm::ArrayRef<uint8_t>(Payload).slice(Offset, Size);
}
