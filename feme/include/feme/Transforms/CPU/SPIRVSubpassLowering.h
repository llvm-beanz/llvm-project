//===- SPIRVSubpassLowering.h - SPIR-V subpassInput ABI plumbing -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::SPIRVSubpassLoweringPass (roadmap F8a): the
// small pass that gives a fragment shader using `feme.stage.subpass.load`
// (see feme::StageOpKind::SubpassLoad, created directly by
// feme::spirv::SubpassLoadPattern in SPIRVToLLVMPatterns.cpp) the two
// trailing ABI parameters -- `subpass_input_heap`/`subpass_input_heap_count`
// -- that FragmentWrapper.cpp's `lowerFragmentSubpassLoad` later reads by
// name, exactly the way `feme::cpu::SPIRVResourceLoweringPass::
// addResourceEnvParams` appends `resource_heap`/`image_heap`/... for an
// ordinary descriptor-bound access.
//
// This is deliberately a separate pass rather than folded into
// SPIRVResourceLoweringPass: a subpass input is not a `spirv.VulkanBuffer`
// handle at all (there is no `llvm.spv.resource.handlefrombinding` call to
// collect a `BoundHandle` from -- see that pass's own header comment's
// scope list), so it does not fit that pass's handle-collection/heap-
// assignment machinery. It reads directly from the currently-bound
// dynamic-rendering color/depth/stencil attachment
// (`feme::vulkan::RenderTargetBinding`), resolved by the shader's own
// `InputAttachmentIndex` decoration through
// `vkCmdSetRenderingInputAttachmentIndices`'s mapping -- not a
// descriptor-set image -- so its heap is a separate array the CPU executor
// (Graphics/Executor.cpp) builds fresh for every draw from the currently
// bound attachments, not from `VkDescriptorSet` state.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SPIRVSUBPASSLOWERING_H
#define FEME_TRANSFORMS_CPU_SPIRVSUBPASSLOWERING_H

#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Appends `subpass_input_heap`/`subpass_input_heap_count` to every function
/// that calls `feme.stage.subpass.load`, so the parameter survives
/// `feme::cpu::SIMDizePass` widening (which passes an original parameter
/// through unchanged) and reaches `feme::cpu::FragmentWrapperPass` by name.
/// A function with no such call is left untouched.
class SPIRVSubpassLoweringPass
    : public llvm::PassInfoMixin<SPIRVSubpassLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-lower-spirv-subpass"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SPIRVSUBPASSLOWERING_H
