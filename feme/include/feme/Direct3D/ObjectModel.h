//===- ObjectModel.h - D3D typed object lifetime model --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DIRECT3D_OBJECTMODEL_H
#define FEME_DIRECT3D_OBJECTMODEL_H

#include "llvm/Support/Error.h"

#include <cstdint>
#include <unordered_map>

namespace feme::direct3d {

enum class ObjectKind : uint32_t {
  Device = 0,
  CommandQueue = 1,
  Allocator = 2,
  Heap = 3,
  Resource = 4,
  PipelineState = 5,
  Fence = 6,
};

struct ObjectHandle {
  ObjectKind Kind = ObjectKind::Device;
  uint64_t Id = 0;
};

class ObjectModel {
public:
  ObjectHandle create(ObjectKind Kind);

  bool isAlive(ObjectHandle Handle) const;
  llvm::Error close(ObjectHandle Handle);
  llvm::Error setPending(ObjectHandle Handle, uint32_t Count);
  llvm::Error reset(ObjectHandle Handle);
  llvm::Error replay(ObjectHandle Handle);

  uint32_t pending(ObjectHandle Handle) const;
  uint32_t replayCount(ObjectHandle Handle) const;

private:
  struct Record {
    bool Alive = true;
    uint32_t Pending = 0;
    uint32_t ReplayCount = 0;
  };

  uint64_t NextId = 1;
  std::unordered_map<uint64_t, Record> Records;
};

} // namespace feme::direct3d

#endif // FEME_DIRECT3D_OBJECTMODEL_H
