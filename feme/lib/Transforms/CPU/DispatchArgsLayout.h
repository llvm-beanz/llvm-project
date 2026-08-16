//===- DispatchArgsLayout.h - FemeDispatchArgs LLVM struct layout -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is private to feme/lib/Transforms/CPU: it declares the LLVM
// struct type mirroring `FemeDispatchArgs`
// (feme/include/feme/Target/CPU/RuntimeABI.h) that both
// `feme::cpu::EntryWrapperPass` (Phase 6) and
// `feme::cpu::ReferenceEntryWrapperPass` (`--reference`, see the "CFG
// restructurization test suite" section of feme/docs/FeMeCPUDesign.md)
// build their `feme_cpu_entry_<name>` wrapper against -- the two passes
// build the same ABI-facing function around differently-shaped bodies (one
// widened, one not), but read the same dispatch arguments out of it.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_TRANSFORMS_CPU_DISPATCHARGSLAYOUT_H
#define FEME_LIB_TRANSFORMS_CPU_DISPATCHARGSLAYOUT_H

#include "StageArgsLayout.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"

#include <array>
#include <cstdint>

namespace feme::cpu {

/// Field indices into the `FemeDispatchArgs`-shaped LLVM struct
/// `getDispatchArgsType` builds, matching
/// feme/include/feme/Target/CPU/RuntimeABI.h field-for-field. `Resources` is
/// the embedded `FemeShaderResources` (roadmap R29); see
/// `ShaderResourcesField`/`getShaderResourcesType` in StageArgsLayout.h for
/// its own fields.
enum DispatchArgsField : unsigned {
  Resources = 0,
  GroupID = 1,
  GroupCount = 2,
  GroupShared = 3,
  Reserved = 4,
};

/// Builds the LLVM struct type mirroring `FemeDispatchArgs`: field
/// types/order match that struct exactly, relying on ordinary LLVM struct
/// layout to reproduce its C layout.
inline llvm::StructType *getDispatchArgsType(llvm::LLVMContext &Ctx) {
  llvm::Type *PtrTy = llvm::PointerType::get(Ctx, 0);
  llvm::Type *I32Ty = llvm::Type::getInt32Ty(Ctx);
  llvm::Type *I32x3 = llvm::ArrayType::get(I32Ty, 3);
  llvm::Type *PtrX4 = llvm::ArrayType::get(PtrTy, 4);
  return llvm::StructType::get(
      Ctx, {getShaderResourcesType(Ctx), I32x3, I32x3, PtrTy, PtrX4});
}

/// Loads field \p Field of `*Args` (a `getDispatchArgsType()`-typed
/// pointer), with \p FieldTy the field's type.
inline llvm::Value *loadArgsField(llvm::IRBuilder<> &Builder,
                                  llvm::StructType *ArgsTy, llvm::Value *Args,
                                  unsigned Field, llvm::Type *FieldTy) {
  llvm::Value *Ptr = Builder.CreateStructGEP(ArgsTy, Args, Field);
  return Builder.CreateLoad(FieldTy, Ptr);
}

/// Loads shader-resources field \p Field (a `ShaderResourcesField` from
/// StageArgsLayout.h) out of `Args->Resources`, the `FemeShaderResources`
/// embedded in the `getDispatchArgsType()`-typed pointer \p Args (roadmap
/// R29).
inline llvm::Value *loadResourcesField(llvm::IRBuilder<> &Builder,
                                       llvm::StructType *ArgsTy,
                                       llvm::Value *Args, unsigned Field,
                                       llvm::Type *FieldTy) {
  llvm::StructType *ResourcesTy = getShaderResourcesType(Builder.getContext());
  llvm::Value *ResourcesPtr =
      Builder.CreateStructGEP(ArgsTy, Args, DispatchArgsField::Resources);
  llvm::Value *FieldPtr =
      Builder.CreateStructGEP(ResourcesTy, ResourcesPtr, Field);
  return Builder.CreateLoad(FieldTy, FieldPtr);
}

/// \p F's thread group dimensions, from `hlsl.numthreads` (see
/// feme::dxil::MetadataRaisingPass). Defaults to `{1, 1, 1}` if absent or
/// malformed.
inline std::array<uint32_t, 3> getThreadGroupSize(const llvm::Function &F) {
  std::array<uint32_t, 3> Size{1, 1, 1};
  if (!F.hasFnAttribute("hlsl.numthreads"))
    return Size;
  llvm::StringRef NumThreads =
      F.getFnAttribute("hlsl.numthreads").getValueAsString();
  llvm::SmallVector<llvm::StringRef, 3> Components;
  NumThreads.split(Components, ',');
  if (Components.size() != 3)
    return Size;
  std::array<uint32_t, 3> Result;
  for (unsigned I = 0; I != 3; ++I)
    if (!llvm::to_integer(Components[I], Result[I], 10))
      return Size;
  return Result;
}

} // namespace feme::cpu

#endif // FEME_LIB_TRANSFORMS_CPU_DISPATCHARGSLAYOUT_H
