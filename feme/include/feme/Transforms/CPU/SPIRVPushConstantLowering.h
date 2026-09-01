//===- SPIRVPushConstantLowering.h - SPIR-V push constant lowering -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::SPIRVPushConstantLoweringPass, which lowers
// SPIR-V's push-constant block into loads from the CPU ABI's inline
// root-constant block (see "Root constants" in feme/docs/FeMeCPUDesign.md
// and roadmap step V3's "map Vulkan push constants onto [root constants]").
//
// Unlike DXIL's root constant (a register-bound `dx.CBuffer` read through
// `llvm.dx.resource.load.cbufferrow.4.*`, see RootConstantLowering.h), a
// SPIR-V push-constant block survives `feme::SPIRVToLLVMTranslator` as an
// ordinary LLVM global variable in address space 13 (the push-constant
// address space LLVM's own SPIR-V backend uses -- see
// `feme::spirv::PushConstantGlobalVariablePattern`'s header comment in
// SPIRVToLLVMPatterns.cpp), accessed through plain `load`/`getelementptr`
// instructions rather than a resource-handle intrinsic. There is at most one
// such global per module (SPIR-V permits only one push-constant block), and
// it is never legal to write to it.
//
// Scope: an access is recognized if it is a direct load of the global
// itself, or a load through a `getelementptr` into it whose indices are all
// compile-time constants (the common "read one push-constant member" shape
// a real shader compiles down to). A dynamically-indexed array member, or
// any other use (a store, a partially-constant GEP, a GEP result used as
// anything but a load), is left entirely alone -- the whole function is
// skipped, for `feme::cpu::checkSupportedRaisedOps` to reject exactly as if
// this pass did not exist, rather than partially rewritten. This mirrors
// `feme::cpu::SPIRVResourceLoweringPass::hasOnlySupportedUses`'s own
// "leave it alone rather than partially lower it" contract.
//
// As with DXIL root constants, a function that also performs bound
// (`spirv.VulkanBuffer` handle) resource access is supported, but lowered
// differently: `feme::cpu::SPIRVResourceLoweringPass` itself finishes the
// job, reusing the `root_constants`/`root_constant_size` parameters it
// already unconditionally appends, rather than this pass adding its own
// (which would collide by name) -- see `matchSPIRVPushConstantAccess`/
// `lowerSPIRVPushConstantAccess`, which both that pass and this one call.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_SPIRVPUSHCONSTANTLOWERING_H
#define FEME_TRANSFORMS_CPU_SPIRVPUSHCONSTANTLOWERING_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"

#include <cstdint>
#include <optional>

namespace llvm {
class Function;
class GlobalVariable;
class Instruction;
class Value;
} // namespace llvm

namespace feme::cpu {

/// One function's complete, supported push-constant access: the module's
/// push-constant global, and every load recognized as reading through it
/// (see the file comment for exactly what is recognized).
struct SPIRVPushConstantAccess {
  llvm::GlobalVariable *Global;
  llvm::SmallVector<llvm::Instruction *, 8> Loads;
};

/// Returns \p F's push-constant access if the module has a push-constant
/// global \p F references, and every one of its uses within \p F matches
/// the recognized shape (see the file comment), or `std::nullopt`
/// otherwise (no push-constant global, \p F does not reference it, or one
/// of its uses is unsupported). Performs no mutation.
std::optional<SPIRVPushConstantAccess>
matchSPIRVPushConstantAccess(llvm::Function &F);

/// Rewrites every load in \p Access into a bounds-checked load from \p
/// RootConstants (zero for any byte range outside \p RootConstantSize's
/// declared span), and erases every instruction \p Access recognized once
/// unused. Returns the highest byte any recognized load actually reads --
/// unlike `feme::cpu::lowerRootConstantAccess`'s own DXIL root constant,
/// which must report its full declared binding size because a dynamic
/// row/array index means there is no fixed set of bytes to inspect
/// statically, every access this pass recognizes has a compile-time-
/// constant byte offset (see the file comment's scope note), so the
/// tighter "bytes actually touched" span is always safe to report instead
/// -- and, on a CPU target whose data layout does not mark vectors as
/// element-aligned, is frequently *narrower* than the push-constant
/// block's own declared-type `DataLayout` store size (e.g. a trailing
/// `int3`/`float3` member's vector alignment rounds its store size up to
/// the next power of two, inflating the whole struct's reported size well
/// past the last byte any such member's own load actually reaches).
/// Reporting the wider, padding-inflated size here would spuriously
/// require a `VkPushConstantRange` to cover bytes no real access ever
/// touches (roadmap L10).
uint32_t lowerSPIRVPushConstantAccess(const SPIRVPushConstantAccess &Access,
                                      llvm::Value *RootConstants,
                                      llvm::Value *RootConstantSize);

/// Lowers a SPIR-V push-constant block access into loads from the CPU ABI's
/// root-constant block, for a function with no bound (`spirv.VulkanBuffer`)
/// resource access of its own -- see the file comment for the combined
/// case, handled by `feme::cpu::SPIRVResourceLoweringPass` instead.
class SPIRVPushConstantLoweringPass
    : public llvm::PassInfoMixin<SPIRVPushConstantLoweringPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() {
    return "feme-cpu-lower-spirv-push-constants";
  }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_SPIRVPUSHCONSTANTLOWERING_H
