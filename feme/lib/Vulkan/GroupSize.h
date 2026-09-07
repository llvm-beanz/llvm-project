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
/// \p EntryPoint cannot be found, declares none of `GLCompute`/`MeshEXT`/
/// `TaskEXT` execution models, or declares none of `LocalSize`/
/// `LocalSizeId`/`BuiltIn WorkgroupSize`.
///
/// (roadmap H6f) The `MeshEXT`/`TaskEXT` acceptance is what lets
/// `GraphicsPipeline.cpp`'s `compileAndValidateStages` reuse this same
/// scanner to validate a mesh or task entry point's own declared group size
/// against `feme::vulkan::MaxMeshWorkGroupSize`/`MaxTaskWorkGroupSize`
/// (`GraphicsPipeline.h`) at pipeline-creation time, exactly the way
/// `Pipeline.cpp`'s `compileComputePipeline` already validates a compute
/// entry's group size against `maxComputeWorkGroupSize`/`Invocations` --
/// the two stages dispatch as bounded workgroups the same way (see this
/// file's own header comment), so their group-size *encoding* in SPIR-V is
/// identical; only the accepted execution model differs.
llvm::Expected<std::array<uint32_t, 3>>
resolveComputeGroupSize(llvm::ArrayRef<uint32_t> Words,
                        llvm::StringRef EntryPoint,
                        llvm::ArrayRef<SpecializationOverride> Overrides);

/// (roadmap L69) Which of `VK_KHR_compute_shader_derivatives`'s two mutually
/// exclusive execution modes \p EntryPoint declares, if any -- `None` if
/// neither `DerivativeGroupQuadsKHR` nor `DerivativeGroupLinearKHR` is
/// present (the common case: a compute entry that never uses `dFdx`/`dFdy`,
/// nor an implicit-LOD `texture()` call that needs one internally, declares
/// neither, per the SPIR-V specification's own "extension requires this
/// execution mode" validation rule for any such use).
enum class ComputeDerivativeGroupMode {
  None,
  /// `DerivativeGroupQuadsKHR`: derivatives are computed across a 2x2 tile
  /// of invocations grouped by their `LocalInvocationId.xy`'s low bit in
  /// each of X and Y (`(2m+{0,1}, 2n+{0,1})`) -- the spatial analogue of a
  /// fragment shader's own guaranteed 2x2 helper-invocation quad. This CPU
  /// target's compute-stage lane assignment (`feme::cpu::SIMDizePass`) is a
  /// flat `LocalInvocationIndex`-ordered widening with no notion of a
  /// spatial X/Y tile at all, so this mode is not yet supported (see
  /// roadmap L69(a)); `resolveComputeDerivativeGroupMode`'s caller rejects
  /// pipeline creation with a clear diagnostic rather than silently
  /// computing a wrong derivative.
  Quads,
  /// `DerivativeGroupLinearKHR`: derivatives are computed across any 4
  /// consecutive `LocalInvocationIndex` values (`4m+{0,1,2,3}`), each
  /// group's own "x"/"y" direction assigned by that index's low/second-low
  /// bit exactly like a fragment quad's own four lanes -- this already
  /// matches this CPU target's own flat, `LocalInvocationIndex`-ordered
  /// lane assignment exactly (`WaveLowering.cpp`'s `flat = WaveIndex * W +
  /// lane` is that same linear order), so `lowerDerivative`'s existing
  /// quad-shuffle math (unchanged since it predates this mode entirely --
  /// it was only ever written for a fragment quad) already computes the
  /// spec-correct answer for this mode with no further change.
  Linear,
};

/// Resolves \p EntryPoint's `VK_KHR_compute_shader_derivatives` execution
/// mode from \p Words, the same native-endian SPIR-V binary module
/// `resolveComputeGroupSize` scans. Returns `ComputeDerivativeGroupMode::
/// None` if \p EntryPoint cannot be found or declares neither mode (not an
/// error: most compute entry points use neither).
llvm::Expected<ComputeDerivativeGroupMode>
resolveComputeDerivativeGroupMode(llvm::ArrayRef<uint32_t> Words,
                                  llvm::StringRef EntryPoint);

} // namespace feme::vulkan

#endif // FEME_LIB_VULKAN_GROUPSIZE_H
