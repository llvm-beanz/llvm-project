//===- Pipeline.h - Shader module / pipeline layout / pipeline -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The V1 shader/pipeline object model (see "Object Model" and "Shader and
// Pipeline Compilation" in feme/docs/FeMeVulkanDesign.md): `VkShaderModule`
// (owned SPIR-V words), `VkPipelineLayout` (restricted to no descriptor
// sets/push constants -- see "Required SPIR-V resource work": descriptors
// are V2, push constants are V3), and `VkPipeline` (a compiled compute
// kernel).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_PIPELINE_H
#define FEME_LIB_VULKAN_PIPELINE_H

#include "llvm/ADT/ArrayRef.h"

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

/// A `VkPipelineLayout`. V1 supports only the resource-free shape needed by
/// "compile and execute a resource-free SPIR-V compute shader using
/// builtins": no descriptor set layouts (V2) and no push-constant ranges
/// (V3), enforced at creation (`vkCreatePipelineLayout` rejects any other
/// shape). Carries no state of its own yet.
class PipelineLayout {};

/// A `VkPipeline` compute pipeline: the compiled CPU kernel `vkCmdDispatch`
/// et al. invoke. Owns the `feme::Context` its compiled code was JIT-ed
/// into, so the code (and the `llvm::LLVMContext` behind it) stay alive for
/// exactly as long as this pipeline does -- see "Compilation flow": "the
/// JIT-compiled code object and the `llvm::LLVMContext` behind it stay
/// alive as long as the `VkPipeline` does".
class ComputePipeline {
public:
  ComputePipeline(std::unique_ptr<feme::Context> Ctx,
                  std::unique_ptr<feme::cpu::CompiledStage> Stage)
      : Ctx(std::move(Ctx)), Stage(std::move(Stage)) {}
  ~ComputePipeline();
  ComputePipeline(ComputePipeline &&) noexcept;
  ComputePipeline &operator=(ComputePipeline &&) noexcept;
  ComputePipeline(const ComputePipeline &) = delete;
  ComputePipeline &operator=(const ComputePipeline &) = delete;

  feme::cpu::CompiledStage &getStage() const { return *Stage; }

private:
  std::unique_ptr<feme::Context> Ctx;
  std::unique_ptr<feme::cpu::CompiledStage> Stage;
};

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_PIPELINE_H
