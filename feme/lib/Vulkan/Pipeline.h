//===- Pipeline.h - Shader module / pipeline layout / pipeline -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The shader/pipeline object model (see "Object Model" and "Shader and
// Pipeline Compilation" in feme/docs/FeMeVulkanDesign.md): `VkShaderModule`
// (owned SPIR-V words), `VkPipelineLayout` (an ordered list of
// `VkDescriptorSetLayout`s -- push constants are still V3), and `VkPipeline`
// (a compiled compute kernel).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_PIPELINE_H
#define FEME_LIB_VULKAN_PIPELINE_H

#include "llvm/ADT/ArrayRef.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace feme {
class Context;
namespace cpu {
class CompiledStage;
} // namespace cpu
} // namespace feme

namespace feme::vulkan {

class DescriptorSetLayout;

/// A `VkShaderModule`: a validated, owned copy of the application's SPIR-V
/// words (see "Input and specialization": "`vkCreateShaderModule` copies
/// the SPIR-V and performs cheap structural checks").
class ShaderModule {
public:
  explicit ShaderModule(std::vector<uint32_t> Words)
      : Words(std::move(Words)) {}

  llvm::ArrayRef<uint32_t> words() const { return Words; }

private:
  std::vector<uint32_t> Words;
};

/// A `VkPipelineLayout`: an ordered list of `VkDescriptorSetLayout`s, plus
/// the declared push-constant ranges (V3: "map Vulkan push constants onto
/// [FeMe root constants]", see "Descriptor Model" in
/// feme/docs/FeMeVulkanDesign.md) -- see the file comment. A (descriptor
/// set, binding) identity is exactly `feme::cpu::BoundResourceRange`'s
/// `(Space, BaseRegister)`, with `Space` equal to the set's index in this
/// list, so no translation table is needed between the two -- see
/// `compileComputePipeline`'s use of this list to validate a shader's
/// bound-resource requirements against it.
class PipelineLayout {
public:
  PipelineLayout(std::vector<const DescriptorSetLayout *> SetLayouts,
                 std::vector<VkPushConstantRange> PushConstantRanges)
      : SetLayouts(std::move(SetLayouts)),
        PushConstantRanges(std::move(PushConstantRanges)) {}

  llvm::ArrayRef<const DescriptorSetLayout *> setLayouts() const {
    return SetLayouts;
  }
  llvm::ArrayRef<VkPushConstantRange> pushConstantRanges() const {
    return PushConstantRanges;
  }

private:
  std::vector<const DescriptorSetLayout *> SetLayouts;
  std::vector<VkPushConstantRange> PushConstantRanges;
};

/// The shareable, compiled part of a `VkPipeline` compute pipeline: the
/// `feme::Context` its compiled code was JIT-ed into (so the code, and the
/// `llvm::LLVMContext` behind it, stay alive as long as anything references
/// this artifact) and the compiled kernel itself. Factored out of
/// `ComputePipeline` (V4) so a `PipelineCache` hit can share one already-
/// compiled artifact between multiple `VkPipeline` handles instead of
/// recompiling -- see PipelineCache.h.
struct CachedPipelineArtifact {
  std::unique_ptr<feme::Context> Ctx;
  std::unique_ptr<feme::cpu::CompiledStage> Stage;
};

/// A `VkPipeline` compute pipeline: a handle sharing ownership of a
/// `CachedPipelineArtifact` -- see "Compilation flow": "the JIT-compiled
/// code object and the `llvm::LLVMContext` behind it stay alive as long as
/// the `VkPipeline` does" (true of every handle sharing the artifact, not
/// just the first one compiled).
class ComputePipeline {
public:
  explicit ComputePipeline(std::shared_ptr<CachedPipelineArtifact> Artifact)
      : Artifact(std::move(Artifact)) {}

  feme::cpu::CompiledStage &getStage() const { return *Artifact->Stage; }

private:
  std::shared_ptr<CachedPipelineArtifact> Artifact;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PIPELINE_H
