//===- AmplificationDispatch.h - Checked task-stage mesh dispatch -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H6d's dispatch-queue half: a task (amplification) entry point's
// `EmitMeshTasksEXT(x, y, z)` call requests a 3D count of mesh workgroups to
// run, mirroring `vkCmdDispatch`'s own group count. Per `VK_EXT_mesh_
// shader`'s "Task Shading" chapter, that request is bounded by the same
// kind of implementation limit compute's own dispatch already enforces
// (`VkPhysicalDeviceMeshShaderPropertiesEXT::maxMeshWorkGroupCount`/
// `maxMeshWorkGroupTotalCount`, mirroring `maxComputeWorkGroupCount`, which
// `feme/lib/Vulkan/CommandBuffer.cpp`'s own `validateGroupCount` already
// checks a compute dispatch against).
//
// `AmplificationDispatchQueue` models that same "validate once, then
// enumerate" shape at the `feme::graphics` layer, independent of Vulkan, so
// it is available to whichever roadmap row (H6c-a-b, once `EmitMeshTasksEXT`
// itself is canonicalized) actually reaches a live task workgroup's
// requested count, and to `Executor::executeDraws`'s own mesh-path chaining
// (roadmap H6e), which needs an already-validated, enumerable sequence of
// mesh workgroup group IDs to dispatch -- exactly what `feme::cpu::
// runDispatch`'s own group-count loop already gives compute.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_AMPLIFICATIONDISPATCH_H
#define FEME_GRAPHICS_AMPLIFICATIONDISPATCH_H

#include "llvm/Support/Error.h"

#include <array>
#include <cstdint>

namespace feme::graphics {

/// The bounds a requested mesh-workgroup dispatch is checked against,
/// mirroring `VkPhysicalDeviceMeshShaderPropertiesEXT::
/// maxMeshWorkGroupCount`/`maxMeshWorkGroupTotalCount` (advertised, once a
/// real Vulkan mesh pipeline exists, by roadmap H6f).
struct AmplificationDispatchLimits {
  /// The per-dimension bound (`maxMeshWorkGroupCount[3]`), mirroring
  /// `VkPhysicalDeviceLimits::maxComputeWorkGroupCount` in shape.
  std::array<uint32_t, 3> MaxGroupCount{};
  /// The bound on the *product* of all three dimensions
  /// (`maxMeshWorkGroupTotalCount`) -- compute has no equivalent limit
  /// (`maxComputeWorkGroupCount` is checked per-dimension only), since a
  /// task entry's `EmitMeshTasksEXT` request has no separate per-dimension
  /// invocation-count multiplier the way compute's own `maxComputeWork
  /// GroupInvocations` covers `LocalSize`; the total group count itself is
  /// what mesh shading bounds instead.
  uint32_t MaxTotalGroupCount = 0;
};

/// A checked, bounded queue of mesh workgroup group IDs a task entry's
/// `EmitMeshTasksEXT(x, y, z)` call requests -- the checked counterpart of
/// `feme::cpu::runDispatch`'s own group-count loop, but returning an
/// enumerable object (mirroring `PreparedDispatch`) rather than eagerly
/// invoking an entry function, since the mesh workgroups it dispatches are
/// not compute groups, and the caller (`Executor::executeDraws`, roadmap
/// H6e) needs to interleave that enumeration with mesh-specific per-
/// workgroup state (its own `MeshOutputBuilder`/payload) rather than a bare
/// callback.
class AmplificationDispatchQueue {
public:
  /// Validates \p GroupCount against \p Limits, per this file's own
  /// comment: every dimension against `Limits.MaxGroupCount`, and the
  /// product of all three (computed in 64 bits, so an over-large per-
  /// dimension request cannot silently wrap back into range) against
  /// `Limits.MaxTotalGroupCount`. Returns an `Error` diagnosing which bound
  /// was exceeded instead of a queue if either check fails.
  static llvm::Expected<AmplificationDispatchQueue>
  create(std::array<uint32_t, 3> GroupCount,
         const AmplificationDispatchLimits &Limits);

  std::array<uint32_t, 3> getGroupCount() const { return GroupCount; }

  /// The total number of mesh workgroups this queue dispatches:
  /// `GroupCount[0] * GroupCount[1] * GroupCount[2]`.
  uint64_t size() const;

  /// The group ID of the \p Index'th queued mesh workgroup (`Index <
  /// size()`), enumerated the same X-fastest, Z-slowest order `feme::cpu::
  /// runDispatch`'s own triple loop already uses.
  std::array<uint32_t, 3> getGroupID(uint64_t Index) const;

private:
  explicit AmplificationDispatchQueue(std::array<uint32_t, 3> GroupCount)
      : GroupCount(GroupCount) {}

  std::array<uint32_t, 3> GroupCount{};
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_AMPLIFICATIONDISPATCH_H
