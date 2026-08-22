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

#include "feme/Core/ShaderStage.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace feme {
class Context;
class Module;
namespace cpu {
class CompiledStage;
struct ResourceInfo;
} // namespace cpu
} // namespace feme

namespace feme::vulkan {

class DescriptorSetLayout;
struct PhysicalDeviceInfo;

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

/// The common base of every `VkPipeline` object, so the single Vulkan
/// handle type can carry either bind point (V6 adds the graphics one) and
/// `vkDestroyPipeline` can free either without knowing which it holds.
class Pipeline {
public:
  enum class Kind : uint8_t {
    Compute,
    Graphics,
  };

  explicit Pipeline(Kind PipelineKind) : PipelineKind(PipelineKind) {}
  virtual ~Pipeline();

  Kind kind() const { return PipelineKind; }

private:
  Kind PipelineKind;
};

/// A `VkPipeline` compute pipeline: a handle sharing ownership of a
/// `CachedPipelineArtifact` -- see "Compilation flow": "the JIT-compiled
/// code object and the `llvm::LLVMContext` behind it stay alive as long as
/// the `VkPipeline` does" (true of every handle sharing the artifact, not
/// just the first one compiled).
class ComputePipeline : public Pipeline {
public:
  explicit ComputePipeline(std::shared_ptr<CachedPipelineArtifact> Artifact)
      : Pipeline(Kind::Compute), Artifact(std::move(Artifact)) {}

  feme::cpu::CompiledStage &getStage() const { return *Artifact->Stage; }

private:
  std::shared_ptr<CachedPipelineArtifact> Artifact;
};

/// Imports \p Words (a `VkShaderModule`'s SPIR-V) into \p Ctx and translates
/// it to LLVM IR, normalizing away the module-level attributes SPIR-V import
/// leaves that have no meaning to `feme::cpu`'s JIT. Shared by the compute
/// and graphics pipeline compilation paths (see "Compilation flow" in
/// feme/docs/FeMeVulkanDesign.md).
llvm::Expected<feme::Module> importShaderModule(feme::Context &Ctx,
                                                llvm::ArrayRef<uint32_t> Words);

/// Whether \p Layout's push-constant ranges visible to \p StageFlags fully
/// cover `[0, RootConstantSize)` with no gap -- see "Descriptor Model":
/// "reject a shader whose accessed range is not fully covered by a range
/// declared in the layout with the [shader] stage bit set".
bool pushConstantsCoverRootConstantSize(const PipelineLayout &Layout,
                                        uint32_t RootConstantSize,
                                        uint32_t MaxPushConstantsSize,
                                        VkShaderStageFlags StageFlags);

/// Checks that every bound range \p Info reports has a compatible binding in
/// \p Layout's descriptor set layouts: the same (set, binding) identity, a
/// descriptor type of the matching class, and a declared array big enough to
/// cover the shader's range. Shared by compute and graphics pipeline
/// creation.
llvm::Error validateBoundRanges(const feme::cpu::ResourceInfo &Info,
                                const PipelineLayout &Layout);

/// Fills a `VkPipelineCreationFeedbackCreateInfo` chained onto \p pNext (if
/// any), for `VK_EXT_pipeline_creation_feedback`/its core-1.3 promotion
/// (roadmap E19). This ICD has no real per-stage compile-timing
/// instrumentation, so every `duration` is honestly reported as `0` rather
/// than a fabricated estimate; `VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT` is
/// always set (a pipeline that reaches this call always finished
/// successfully -- an error return skips it entirely), with
/// `VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT` added
/// when \p CacheHit reflects a `VkPipelineCache` hit. \p StageCount is
/// `VkGraphicsPipelineCreateInfo::stageCount` (one call per stage) or `1`
/// for a compute pipeline's single stage; a chained
/// `pipelineStageCreationFeedbackCount` that disagrees with it is clamped
/// to the smaller of the two, matching the specification's own "must be
/// stageCount" requirement being an application bug this ICD survives
/// rather than crashes on.
void fillPipelineCreationFeedback(const void *pNext, uint32_t StageCount,
                                  bool CacheHit);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PIPELINE_H
