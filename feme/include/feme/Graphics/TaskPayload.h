//===- TaskPayload.h - Bounded task-stage payload storage -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Roadmap H6c's task-stage counterpart to `feme::graphics::
// GeometryStreamBuilder`/`MeshOutputBuilder`: bounded storage for a task
// (amplification) entry point's payload -- the block of data a task
// workgroup writes once (SPIR-V's `TaskPayloadWorkgroupEXT` storage class)
// and every mesh workgroup it dispatches via `EmitMeshTasksEXT` reads back
// identically, per `VK_EXT_mesh_shader`.
//
// Unlike `MeshOutputBuilder`'s structured, signature-shaped per-vertex/
// per-primitive rows, a task payload has no `FemeStageLayout` structure of
// its own visible to this storage: it is exactly as large as the task
// shader's own payload type (an arbitrary, shader-defined struct) and is
// written/read as raw bytes, the same "flat, host-owned memory" shape every
// other bounded stage-output storage in this codebase already uses. This
// class is therefore a bounded byte buffer, not a scalar/row one: the
// bound is a byte count (`MaxPayloadBytes`, a Vulkan-implementation-defined
// limit -- `VkPhysicalDeviceMeshShaderPropertiesEXT::maxTaskPayloadSize` --
// this implementation's own honest ceiling, mirroring how `MeshState`'s own
// maxima are the entry point's *declared* limits, not a hardware one),
// checked the same way every other bounded builder in this file's siblings
// already checks its own before every write.
//
// Roadmap H6c-a-b wires a real compiled task entry point's payload writes
// into a live `TaskPayloadBuilder` object: `Executor.cpp` hands
// `getMutableBytes()`'s pointer to `FemeTaskArgs::Payload` before invoking
// a compiled task stage, so `feme::cpu::TaskPayloadWrapperPass`'s lowered
// payload store (roadmap H6h/H6i having given `TaskPayloadWorkgroupEXT` an
// address-space convention and a canonicalized `feme.stage.*` op) writes
// straight into this class's own backing storage, and every mesh
// workgroup a task workgroup dispatches (`EmitMeshTasksEXT`, roadmap H6d)
// reads that same builder's `getBytes()` back via `FemeMeshArgs::Payload`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_TASKPAYLOAD_H
#define FEME_GRAPHICS_TASKPAYLOAD_H

#include "llvm/ADT/ArrayRef.h"

#include <cstdint>
#include <vector>

namespace feme::graphics {

/// Bounded storage for one task workgroup's payload, per the file comment
/// above.
class TaskPayloadBuilder {
public:
  /// \p MaxPayloadBytes bounds every `write` below -- the implementation's
  /// own advertised `maxTaskPayloadSize` ceiling (or the task shader's own
  /// declared payload type size, whichever a caller chooses to construct
  /// this with).
  explicit TaskPayloadBuilder(uint32_t MaxPayloadBytes);

  uint32_t getMaxPayloadBytes() const {
    return static_cast<uint32_t>(Payload.size());
  }

  /// Writes \p Bytes at byte offset \p Offset. Returns false, leaving
  /// storage unmodified, if `Offset + Bytes.size()` exceeds
  /// `getMaxPayloadBytes()` -- an out-of-bounds payload write is a real
  /// authoring error (a payload struct larger than the shader itself
  /// declared), diagnosed rather than silently truncated or overrun.
  bool write(uint32_t Offset, llvm::ArrayRef<uint8_t> Bytes);

  /// The bytes written to `[Offset, Offset + Size)`. Returns an empty
  /// range if that window exceeds `getMaxPayloadBytes()`. Bytes never
  /// written by `write` read back as zero (this builder's storage is
  /// zero-initialized at construction, matching every other bounded
  /// output storage in this codebase, which the caller is expected to
  /// zero-initialize before a compiled stage's own writes begin).
  llvm::ArrayRef<uint8_t> read(uint32_t Offset, uint32_t Size) const;

  /// The whole payload buffer, `getMaxPayloadBytes()` wide.
  llvm::ArrayRef<uint8_t> getBytes() const { return Payload; }

  /// The whole payload buffer, mutable: roadmap H6c-a-b's own use for this
  /// accessor is handing a compiled task entry's `FemeTaskArgs::Payload`
  /// pointer (`Executor.cpp`) somewhere real to store into directly --
  /// `feme::cpu::TaskPayloadWrapperPass`'s lowered payload store writes
  /// straight into this backing memory via that raw pointer, bypassing
  /// `write` above entirely (a compiled task entry has no way to call back
  /// into this class), so this is the only way this class's own storage
  /// ever picks up a real compiled write.
  llvm::MutableArrayRef<uint8_t> getMutableBytes() { return Payload; }

private:
  std::vector<uint8_t> Payload;
};

} // namespace feme::graphics

#endif // FEME_GRAPHICS_TASKPAYLOAD_H
