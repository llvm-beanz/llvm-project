//===- ObjectModel.cpp - D3D typed object lifetime model ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#include "feme/Direct3D/ObjectModel.h"

#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace feme::direct3d {

ObjectHandle ObjectModel::create(ObjectKind Kind) {
  ObjectHandle Handle{Kind, NextId++};
  Records.emplace(Handle.Id, Record{});
  return Handle;
}

bool ObjectModel::isAlive(ObjectHandle Handle) const {
  auto It = Records.find(Handle.Id);
  return It != Records.end() && It->second.Alive;
}

llvm::Error ObjectModel::close(ObjectHandle Handle) {
  auto It = Records.find(Handle.Id);
  if (It == Records.end() || !It->second.Alive)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "object handle is not alive");
  if (It->second.Pending != 0)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "object cannot close while work is pending");
  It->second.Alive = false;
  return llvm::Error::success();
}

llvm::Error ObjectModel::setPending(ObjectHandle Handle, uint32_t Count) {
  auto It = Records.find(Handle.Id);
  if (It == Records.end() || !It->second.Alive)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "object handle is not alive");
  It->second.Pending = Count;
  return llvm::Error::success();
}

llvm::Error ObjectModel::reset(ObjectHandle Handle) {
  auto It = Records.find(Handle.Id);
  if (It == Records.end() || !It->second.Alive)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "object handle is not alive");
  if (It->second.Pending != 0)
    return llvm::createStringError(
        llvm::errc::invalid_argument,
        "allocator cannot reset while work is pending");
  if (Handle.Kind != ObjectKind::Allocator)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "reset is only valid for allocators");
  return llvm::Error::success();
}

llvm::Error ObjectModel::replay(ObjectHandle Handle) {
  auto It = Records.find(Handle.Id);
  if (It == Records.end() || !It->second.Alive)
    return llvm::createStringError(llvm::errc::invalid_argument,
                                   "object handle is not alive");
  ++It->second.ReplayCount;
  return llvm::Error::success();
}

uint32_t ObjectModel::pending(ObjectHandle Handle) const {
  auto It = Records.find(Handle.Id);
  return It == Records.end() ? 0 : It->second.Pending;
}

uint32_t ObjectModel::replayCount(ObjectHandle Handle) const {
  auto It = Records.find(Handle.Id);
  return It == Records.end() ? 0 : It->second.ReplayCount;
}

} // namespace feme::direct3d
