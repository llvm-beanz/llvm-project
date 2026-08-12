//===- ResourceCalls.h - `feme.cpu.resource.*` call helpers ------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the creation and recognition helpers for the canonical,
// type-mangled `feme.cpu.resource.*` calls described in the "Lowering"
// subsection of the "Resource Model" section of feme/docs/FeMeCPUDesign.md.
// `feme::cpu::ResourceLoweringPass` is the only current producer of these
// calls; later phases (widening, wave lowering, the entry wrapper) and the
// eventual `ResourceCallOptimizationPass` are consumers, which is why the
// shape is centralized here rather than duplicated at each use site.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_CPU_RESOURCECALLS_H
#define FEME_TRANSFORMS_CPU_RESOURCECALLS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Value.h"

#include <optional>

namespace llvm {
class CallInst;
class Function;
class IRBuilderBase;
class Module;
class Type;
} // namespace llvm

namespace feme::cpu {

/// Which canonical resource access a `feme.cpu.resource.*` call performs.
/// See "Lowering" in feme/docs/FeMeCPUDesign.md: typed-buffer accesses go
/// through the format-aware `Typed` family, keyed by descriptor index and
/// element index; raw and structured buffers -- which have no runtime
/// format to convert -- share the `Raw` family, keyed by descriptor index
/// and a byte offset (an already-computed `element_index * Stride` for a
/// structured buffer, or the byte address itself for an unstructured one).
enum class ResourceCallKind : uint8_t {
  LoadTyped,
  StoreTyped,
  LoadRaw,
  StoreRaw,
};

/// Returns whether \p Kind reads or writes through the resource.
bool isLoad(ResourceCallKind Kind);

/// The heap/root-constant operands every `feme.cpu.resource.*` call carries
/// alongside its access-specific operands (see "Lowering": "The heap
/// operands come from new function parameters"). These are always values
/// already available in the caller -- typically the trailing parameters
/// `feme::cpu::addResourceEnvParams` appends to a rewritten function.
struct ResourceCallEnv {
  llvm::Value *ResourceHeap = nullptr;
  llvm::Value *ResourceHeapCount = nullptr;
  llvm::Value *SamplerHeap = nullptr;
  llvm::Value *SamplerHeapCount = nullptr;
  llvm::Value *RootConstants = nullptr;
  llvm::Value *RootConstantSize = nullptr;
};

/// The result of successfully matching a call against the canonical
/// `feme.cpu.resource.*` shape (see `matchResourceCall`).
struct MatchedResourceCall {
  ResourceCallKind Kind;
  llvm::CallInst *Call = nullptr;
  /// The element type a typed access converts to/from, or the raw access's
  /// value type.
  llvm::Type *ElementType = nullptr;
  ResourceCallEnv Env;
  llvm::Value *DescriptorIndex = nullptr;
  /// The element index (`Typed`) or byte offset (`Raw`) operand.
  llvm::Value *Offset = nullptr;
  /// The stored value operand, for `StoreTyped`/`StoreRaw`; null for a load.
  llvm::Value *StoredValue = nullptr;
  llvm::Value *Mask = nullptr;
};

/// Returns the type-mangled `feme.cpu.resource.*` name for \p Kind and
/// \p ElementType, e.g. `feme.cpu.resource.load.typed.v4f32` -- the literal
/// example in "Lowering". Asserts \p ElementType is one of the scalar/vector
/// types the canonical calls support (see the "Descriptor formats" section:
/// integer/floating-point scalars and fixed vectors of them).
std::string mangleResourceCallName(ResourceCallKind Kind,
                                   llvm::Type *ElementType);

/// Gets (inserting if absent) the `feme.cpu.resource.*` declaration for
/// \p Kind and \p ElementType in \p M, with the canonical signature and
/// memory-effect attributes "Lowering" describes (an ordinary declaration
/// rather than an intrinsic, since these are FeMe-private helpers, not part
/// of LLVM's IR vocabulary).
llvm::Function *getOrInsertResourceCall(llvm::Module &M, ResourceCallKind Kind,
                                        llvm::Type *ElementType);

/// Builds a `feme.cpu.resource.load.typed.*` call reading the element at
/// \p ElementIndex through descriptor \p DescriptorIndex, returning
/// \p ElementType.
llvm::CallInst *createTypedLoad(llvm::IRBuilderBase &Builder,
                                const ResourceCallEnv &Env,
                                llvm::Value *DescriptorIndex,
                                llvm::Value *ElementIndex, llvm::Value *Mask,
                                llvm::Type *ElementType,
                                const llvm::Twine &Name = "");

/// Builds a `feme.cpu.resource.store.typed.*` call writing \p StoredValue to
/// the element at \p ElementIndex through descriptor \p DescriptorIndex.
llvm::CallInst *createTypedStore(llvm::IRBuilderBase &Builder,
                                 const ResourceCallEnv &Env,
                                 llvm::Value *DescriptorIndex,
                                 llvm::Value *ElementIndex,
                                 llvm::Value *StoredValue, llvm::Value *Mask);

/// Builds a `feme.cpu.resource.load.raw.*` call reading a value of type
/// \p ElementType at \p ByteOffset through descriptor \p DescriptorIndex.
llvm::CallInst *createRawLoad(llvm::IRBuilderBase &Builder,
                              const ResourceCallEnv &Env,
                              llvm::Value *DescriptorIndex,
                              llvm::Value *ByteOffset, llvm::Value *Mask,
                              llvm::Type *ElementType,
                              const llvm::Twine &Name = "");

/// Builds a `feme.cpu.resource.store.raw.*` call writing \p StoredValue at
/// \p ByteOffset through descriptor \p DescriptorIndex.
llvm::CallInst *createRawStore(llvm::IRBuilderBase &Builder,
                               const ResourceCallEnv &Env,
                               llvm::Value *DescriptorIndex,
                               llvm::Value *ByteOffset,
                               llvm::Value *StoredValue, llvm::Value *Mask);

/// Recognizes \p CI as one of the canonical `feme.cpu.resource.*` calls,
/// returning its decoded operands, or `std::nullopt` if \p CI's callee isn't
/// one (including a call to some other, unrelated function that merely
/// happens to be named `feme.cpu.resource.*`, which is not a call this
/// module itself produced).
std::optional<MatchedResourceCall> matchResourceCall(const llvm::CallInst &CI);

} // namespace feme::cpu

#endif // FEME_TRANSFORMS_CPU_RESOURCECALLS_H
