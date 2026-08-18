//===- GroupSize.h - SPIR-V compute group-size resolution ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Resolves a compute shader's thread-group size directly from a raw SPIR-V
// binary module, per "Input and specialization" in
// feme/docs/FeMeVulkanDesign.md: the `LocalSize` execution mode, the
// `LocalSizeId` execution mode (whose operands are specialization-constant
// ids), and the deprecated `BuiltIn WorkgroupSize` decoration applied to a
// specialization-constant composite (which overrides `LocalSize` when
// present, and is what glslang emits by default).
//
// This is a deliberate, narrowly-scoped exception to "use SPIR-V/MLIR
// structured APIs rather than patching binary words" (see that same
// design section): MLIR's `mlir::spirv::deserialize` does not preserve a
// `BuiltIn` decoration applied to an `OpSpecConstantComposite`/
// `OpConstantComposite` at all -- `OpSpecConstantComposite`/
// `OpConstantComposite` are deserialized through dedicated processing
// functions (`Deserializer::processSpecConstantComposite`/
// `processConstantComposite` in
// mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp) that never
// consult the generic per-result-id `decorations` map the way ops handled
// through the auto-generated instruction table do (see
// `DeserializeOps.cpp`'s "Attach attributes from decorations" comment), so
// there is no structured API this could recover the value from even in
// principle. This scanner is Vulkan-specific group-size resolution logic
// (the priority ordering and specialization-constant override rules are
// spec-mandated, not a general SPIR-V/LLVM IR concern), so it lives here
// rather than as a change to the shared
// `feme::spirv::createConvertSPIRVToLLVMPass`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_GROUPSIZE_H
#define FEME_LIB_VULKAN_GROUPSIZE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>

namespace feme::vulkan {

/// One `VkSpecializationMapEntry`'s effect on a 32-bit-typed specialization
/// constant: overrides the constant identified by `ConstantID` (SPIR-V's
/// `SpecId` decoration value, not the map entry's own `constantID` field's
/// unrelated array index) to `Value`.
struct SpecializationOverride {
  uint32_t ConstantID = 0;
  uint32_t Value = 0;
};

/// Resolves \p EntryPoint's thread-group size from \p Words, a native-endian
/// SPIR-V binary module (the same word array `feme::SPIRVImporter` consumes
/// -- see its `import` implementation), applying \p Overrides to any
/// specialization constant the resolution depends on. Returns an error if
/// \p EntryPoint cannot be found, declares no `GLCompute` execution model,
/// or declares none of `LocalSize`/`LocalSizeId`/`BuiltIn WorkgroupSize`.
llvm::Expected<std::array<uint32_t, 3>>
resolveComputeGroupSize(llvm::ArrayRef<uint32_t> Words,
                        llvm::StringRef EntryPoint,
                        llvm::ArrayRef<SpecializationOverride> Overrides);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_GROUPSIZE_H
