//===- TaskPayloadWrapper.h - Task stage payload store lowering ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::cpu::TaskPayloadWrapperPass, roadmap H6c-a-b:
// lowers a task (amplification) entry point's canonicalized
// `feme.stage.task.payload.store` writes (roadmap H6i's constant byte
// offset, roadmap H6h's address-space-14 import pattern) into
// `FemeTaskArgs::Payload` -- the bounded storage
// `feme::graphics::TaskPayloadBuilder` (roadmap H6c) already reserves for
// it, and the same block a mesh workgroup `EmitMeshTasksEXT` dispatches
// reads back through `FemeMeshArgs::Payload` (roadmap H6d's dispatch
// queue).
//
// Mirrors `feme::cpu::MeshOutputWrapperPass`'s own split from
// `feme::cpu::EntryWrapperPass`: this pass runs first, appending the two
// trailing wave-body parameters (`task_payload`, `task_max_payload_bytes`)
// a lowered payload store addresses, and `EntryWrapperPass` (extended by
// this roadmap row with its own `IsTask` handling, mirroring `IsMesh`)
// fills them from a real `FemeTaskArgs*` by name -- the same "recovered by
// name" convention every other wave-body parameter already uses.
//
// Unlike `MeshOutputWrapperPass`'s per-vertex/per-primitive addressing
// (which needs a `FemeStageLayout` to turn an `ElementID` into a byte
// offset), a task payload store's own `offset` operand is already a
// resolved, compile-time-constant byte offset into raw, shader-defined
// memory (`StageOpKind::TaskPayloadStore`'s own comment) -- there is no
// signature element, row, or component to look up, so this pass's own
// lowering is a plain (masked, defensively bounds-checked) store to
// `task_payload + offset`, not an address computed through a layout table.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_TASKPAYLOADWRAPPER_H
#define FEME_TRANSFORMS_CPU_TASKPAYLOADWRAPPER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/PassManager.h"

namespace feme::cpu {

/// Roadmap H6c-a-b: lowers a task entry's `feme.stage.task.payload.store`
/// writes into `FemeTaskArgs::Payload`. See the file comment above for
/// scope.
class TaskPayloadWrapperPass
    : public llvm::PassInfoMixin<TaskPayloadWrapperPass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);

  static llvm::StringRef name() { return "feme-cpu-wrap-task-payload"; }
};

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_TASKPAYLOADWRAPPER_H
