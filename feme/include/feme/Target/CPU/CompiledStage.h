//===- CompiledStage.h - FeMe CPU target compiled-code object -----*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::CompiledStage, the compiled-code ownership
// object shared by every CPU-target stage. It owns the ORC JIT instance, the
// compiled entry point in it, and the reflection needed to prepare a compute
// dispatch or a vertex/fragment batch against it.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_COMPILEDSTAGE_H
#define FEME_TARGET_CPU_COMPILEDSTAGE_H

#include "feme/Target/CPU/Pipeline.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Target/CPU/ResourceInfo.h"
#include "feme/Transforms/CPU/GroupSharedInfo.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
namespace orc {
class LLJIT;
} // namespace orc
} // namespace llvm

namespace feme {
class Context;
class Module;
} // namespace feme

namespace feme::cpu {

struct JITOptions;

class CompiledStage {
public:
  static llvm::Expected<std::unique_ptr<CompiledStage>>
  create(Context &Ctx, feme::Module M, const JITOptions &Opts);

  static llvm::Expected<std::unique_ptr<CompiledStage>>
  create(Context &Ctx, feme::Module M, const StageCompileOptions &Opts);

  ~CompiledStage();
  CompiledStage(CompiledStage &&) noexcept;
  CompiledStage &operator=(CompiledStage &&) noexcept;
  CompiledStage(const CompiledStage &) = delete;
  CompiledStage &operator=(const CompiledStage &) = delete;

  ShaderStage getStage() const { return Stage; }
  const ResourceInfo &getResourceInfo() const { return Info; }
  unsigned getWaveSize() const { return WaveSize; }
  std::array<uint32_t, 3> getGroupSize() const { return GroupSize; }

  StageArtifactInfo getArtifactInfo() const;

  llvm::Error invokeGroup(const PreparedDispatch &Prepared,
                          std::array<uint32_t, 3> GroupID,
                          llvm::MutableArrayRef<uint8_t> GroupShared) const;

  llvm::Error invokeVertices(const PreparedVertexBatch &Prepared) const;
  llvm::Error invokeFragments(const PreparedFragmentBatch &Prepared) const;
  llvm::Error invokePatch(const PreparedPatchBatch &Prepared) const;
  llvm::Error
  invokePatchConstant(const PreparedPatchConstantBatch &Prepared) const;

  CompiledStage(std::unique_ptr<llvm::orc::LLJIT> JIT, void *EntryFn,
                ShaderStage Stage, ResourceInfo Info, unsigned WaveSize,
                std::array<uint32_t, 3> GroupSize,
                GroupSharedRequirements GroupSharedReqs,
                uint32_t SideEffectFlags, std::vector<uint8_t> Signature);

  std::unique_ptr<llvm::orc::LLJIT> JIT;
  void *EntryFn;
  ShaderStage Stage = ShaderStage::Compute;
  ResourceInfo Info;
  unsigned WaveSize = 1;
  std::array<uint32_t, 3> GroupSize = {1, 1, 1};
  GroupSharedRequirements GroupSharedReqs;
  uint32_t SideEffectFlags = 0;
  std::vector<uint8_t> Signature;
};

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_COMPILEDSTAGE_H
