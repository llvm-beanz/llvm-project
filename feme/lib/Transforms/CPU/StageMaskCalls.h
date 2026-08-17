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

bool isMaskedOutputStoreCall(const llvm::CallInst &CI);
bool isMaskedStreamEmitCall(const llvm::CallInst &CI);
bool isMaskedStreamCutCall(const llvm::CallInst &CI);
bool isReturnMasksCall(const llvm::CallInst &CI);

} // namespace feme::cpu

#endif // FEME_LIB_TRANSFORMS_CPU_STAGEMASKCALLS_H
