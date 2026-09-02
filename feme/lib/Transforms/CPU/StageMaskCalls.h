//===- StageMaskCalls.h - CPU-internal masked stage side effects -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Private to feme/lib/Transforms/CPU: CPU-internal helper calls that carry the
// per-lane side-effect mask `LinearizePass` threads onto stage-output stores so
// later phases can widen and lower them the same way they already do resource
// stores.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_TRANSFORMS_CPU_STAGEMASKCALLS_H
#define FEME_LIB_TRANSFORMS_CPU_STAGEMASKCALLS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/InstrTypes.h"

namespace llvm {
class CallInst;
class Function;
class IRBuilderBase;
class Module;
class Type;
class Value;
} // namespace llvm

namespace feme::cpu {

// Roadmap R34's continuation: `feme.stage.stream.emit`/`.cut` are
// side-effecting the same way `feme.stage.output.store` is (see
// GeometryWrapper.cpp's file comment and "Patch and geometry wrappers" in
// feme/docs/FeMeGraphicsDesign.md, "emission is side-effecting even when no
// framebuffer write occurs"), so `LinearizePass` threads the same per-lane
// side-effect mask onto masked variants of them, exactly as it already does
// for output stores.
inline constexpr llvm::StringLiteral MaskedOutputStorePrefix =
    "feme.cpu.masked.stage.output.store";
inline constexpr llvm::StringLiteral MaskedStreamEmitPrefix =
    "feme.cpu.masked.stage.stream.emit";
inline constexpr llvm::StringLiteral MaskedStreamCutPrefix =
    "feme.cpu.masked.stage.stream.cut";
inline constexpr llvm::StringLiteral ReturnMasksPrefix =
    "feme.cpu.stage.return.masks";
/// Roadmap H6c-a-b: `feme.stage.task.payload.store` is side-effecting the
/// same way `feme.stage.output.store` is (a task workgroup's bounded
/// payload write, per `StageOpKind::TaskPayloadStore`'s own comment), so
/// `LinearizePass` threads the same per-lane side-effect mask onto a
/// masked variant of it too.
inline constexpr llvm::StringLiteral MaskedTaskPayloadStorePrefix =
    "feme.cpu.masked.task.payload.store";
/// Roadmap H6c-a-a-i: `feme.stage.set_mesh_outputs` is side-effecting the
/// same way `feme.stage.task.payload.store` is (a mesh workgroup's
/// once-per-workgroup declared output counts), so `LinearizePass` threads
/// the same per-lane side-effect mask onto a masked variant of it too.
inline constexpr llvm::StringLiteral MaskedSetMeshOutputsPrefix =
    "feme.cpu.masked.set_mesh_outputs";
/// Roadmap H6s: `feme.stage.emit_mesh_tasks` is side-effecting the same way
/// `feme.stage.set_mesh_outputs` is (a task workgroup's once-per-workgroup
/// requested mesh-dispatch group count), so `LinearizePass` threads the
/// same per-lane side-effect mask onto a masked variant of it too.
inline constexpr llvm::StringLiteral MaskedEmitMeshTasksPrefix =
    "feme.cpu.masked.emit_mesh_tasks";

inline llvm::StringRef getMaskedOutputStoreName() {
  return MaskedOutputStorePrefix;
}

inline llvm::StringRef getReturnMasksName() { return ReturnMasksPrefix; }

llvm::FunctionCallee
getOrInsertMaskedOutputStore(llvm::Module &M, llvm::Type *ValueTy,
                             llvm::Type *RowTy, llvm::Type *ComponentTy,
                             llvm::Type *VertexTy, llvm::Type *MaskTy);

llvm::CallInst *createMaskedOutputStore(llvm::IRBuilderBase &B,
                                        uint32_t ElementID, llvm::Value *Row,
                                        llvm::Value *Component,
                                        llvm::Value *Value, llvm::Value *Vertex,
                                        llvm::Value *Mask);

llvm::FunctionCallee getOrInsertMaskedStreamEmit(llvm::Module &M,
                                                 llvm::Type *StreamTy,
                                                 llvm::Type *MaskTy);

llvm::CallInst *createMaskedStreamEmit(llvm::IRBuilderBase &B,
                                       llvm::Value *Stream, llvm::Value *Mask);

llvm::FunctionCallee getOrInsertMaskedStreamCut(llvm::Module &M,
                                                llvm::Type *StreamTy,
                                                llvm::Type *MaskTy);

llvm::CallInst *createMaskedStreamCut(llvm::IRBuilderBase &B,
                                      llvm::Value *Stream, llvm::Value *Mask);

llvm::FunctionCallee getOrInsertReturnMasks(llvm::Module &M, llvm::Type *LiveTy,
                                            llvm::Type *SideEffectTy);

llvm::CallInst *createReturnMasks(llvm::IRBuilderBase &B, llvm::Value *Live,
                                  llvm::Value *SideEffect);

/// `feme.cpu.masked.task.payload.store(offset, value, mask)`: \p Offset is
/// the constant byte offset `StageOpKind::TaskPayloadStore`'s own comment
/// documents, carried through unchanged (mirroring `Element` in
/// `createMaskedOutputStore`, also never widened).
llvm::FunctionCallee getOrInsertMaskedTaskPayloadStore(llvm::Module &M,
                                                       llvm::Type *ValueTy,
                                                       llvm::Type *MaskTy);

llvm::CallInst *createMaskedTaskPayloadStore(llvm::IRBuilderBase &B,
                                             llvm::Value *Offset,
                                             llvm::Value *Value,
                                             llvm::Value *Mask);

/// `feme.cpu.masked.set_mesh_outputs(vertex_count, primitive_count, mask)`:
/// both counts are carried through per-lane (mangled by \p MaskTy, the only
/// type that varies between the scalar call `LinearizePass` creates and the
/// widened one `FunctionWidener` later creates, mirroring
/// `getOrInsertMaskedStreamEmit`'s own convention) so `feme::cpu::
/// MeshOutputWrapperPass` can pick any one active lane's (spec-identical)
/// value once lowered.
llvm::FunctionCallee getOrInsertMaskedSetMeshOutputs(llvm::Module &M,
                                                     llvm::Type *CountTy,
                                                     llvm::Type *MaskTy);

llvm::CallInst *createMaskedSetMeshOutputs(llvm::IRBuilderBase &B,
                                           llvm::Value *VertexCount,
                                           llvm::Value *PrimitiveCount,
                                           llvm::Value *Mask);

/// `feme.cpu.masked.emit_mesh_tasks(group_count_x, group_count_y,
/// group_count_z, mask)`: all three counts are carried through per-lane
/// (mangled by \p MaskTy, mirroring `getOrInsertMaskedSetMeshOutputs`'s own
/// convention) so `feme::cpu::TaskPayloadWrapperPass` can pick any one
/// active lane's (spec-identical) value once lowered.
llvm::FunctionCallee getOrInsertMaskedEmitMeshTasks(llvm::Module &M,
                                                    llvm::Type *CountTy,
                                                    llvm::Type *MaskTy);

llvm::CallInst *createMaskedEmitMeshTasks(llvm::IRBuilderBase &B,
                                          llvm::Value *GroupCountX,
                                          llvm::Value *GroupCountY,
                                          llvm::Value *GroupCountZ,
                                          llvm::Value *Mask);

bool isMaskedOutputStoreCall(const llvm::CallInst &CI);
bool isMaskedStreamEmitCall(const llvm::CallInst &CI);
bool isMaskedStreamCutCall(const llvm::CallInst &CI);
bool isReturnMasksCall(const llvm::CallInst &CI);
bool isMaskedTaskPayloadStoreCall(const llvm::CallInst &CI);
bool isMaskedSetMeshOutputsCall(const llvm::CallInst &CI);
bool isMaskedEmitMeshTasksCall(const llvm::CallInst &CI);

} // namespace feme::cpu

#endif // FEME_LIB_TRANSFORMS_CPU_STAGEMASKCALLS_H
