//===- SpecializationPatch.h - Raw SPIR-V spec-constant override patch --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Applies a real `VkSpecializationInfo`'s overrides directly to a raw
// SPIR-V binary module's own `OpSpecConstant` literal default-value words,
// in place, before `feme::SPIRVImporter`/`mlir::spirv::deserialize` ever
// sees the module.
//
// This exists because `mlir::spirv::deserialize` has no notion of
// specialization at all: it folds every specialization constant it
// encounters to that constant's own module-declared *default* value, with
// no way to plug in the pipeline-creation-time `VkSpecializationInfo`
// override afterwards for anything the deserializer itself resolves the
// constant's value for -- most notably `processArrayType`'s own
// `resolveConstantArrayLength` (roadmap L7k), which needs an `OpTypeArray`'s
// length as a concrete `spirv::ArrayType` size at deserialization time, long
// before any later `feme`-side pass ever sees a real override. Patching the
// constant's own literal word beforehand, so the deserializer's existing
// default-value fold naturally picks up the real value, needs no change to
// `resolveConstantArrayLength`/`processArrayType` at all, and fixes every
// other spec-constant-driven shape a future SPIR-V module might exercise the
// same way, not merely this one array-length case (roadmap L7p).
//
// Like GroupSize.h's own scanner, this is a deliberate, narrowly-scoped
// exception to "use SPIR-V/MLIR structured APIs rather than patching binary
// words" (feme/docs/FeMeVulkanDesign.md, "Input and specialization"): there
// is no structured API to apply a specialization override to an
// already-deserialized MLIR module's spec constant (the constant is folded
// away entirely -- see GroupSize.h's own header comment for the analogous
// `OpSpecConstantComposite`/`BuiltIn` case), so the raw binary is patched
// before deserialization instead.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_VULKAN_SPECIALIZATIONPATCH_H
#define FEME_LIB_VULKAN_SPECIALIZATIONPATCH_H

#include "GroupSize.h"

#include "llvm/ADT/ArrayRef.h"

namespace feme::vulkan {

/// Overwrites every `OpSpecConstant`'s own literal value word in \p Words
/// (a mutable, native-endian SPIR-V binary module -- e.g. a private copy of
/// a `feme::vulkan::ShaderModule`'s own immutable words, never that
/// storage itself, since a single shader module may be reused by multiple
/// pipelines with different overrides) with the real value \p Overrides
/// supplies for that constant's own `SpecId` decoration. A specialization
/// constant \p Overrides does not name (no matching `SpecId`, or no
/// `SpecId` decoration at all) is left at its own module-declared default,
/// matching Vulkan's own "an unreferenced map entry has no effect"
/// specialization semantics. Only the first literal value word of each
/// `OpSpecConstant` is ever written, matching `SpecializationOverride`'s
/// own narrowed 32-bit-value-only scope (see `Pipeline.cpp`'s
/// `buildSpecializationOverrides`).
void patchSpecializationConstants(
    llvm::MutableArrayRef<uint32_t> Words,
    llvm::ArrayRef<SpecializationOverride> Overrides);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_SPECIALIZATIONPATCH_H
