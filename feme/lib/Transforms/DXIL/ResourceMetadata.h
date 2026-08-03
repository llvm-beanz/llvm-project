//===- ResourceMetadata.h - Read DXIL's dx.resources metadata ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a reader for DXIL's `!dx.resources` named metadata,
// which describes every resource a DXIL module binds. It is private to
// feme/lib/Transforms/DXIL: only the DXIL raising passes need it, and it
// models DXIL's frozen metadata encoding rather than anything FeMe exposes.
//
// DXIL's pre-SM6.6 `CreateHandle` op (opcode 57) identifies the resource it
// creates a handle for indirectly, by (resource class, range ID) -- an index
// into these metadata lists -- rather than carrying the binding inline the
// way the newer `CreateHandleFromBinding` op (217) does. Raising those calls
// therefore requires reading the lists back.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_LIB_TRANSFORMS_DXIL_RESOURCEMETADATA_H
#define FEME_LIB_TRANSFORMS_DXIL_RESOURCEMETADATA_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/DXILABI.h"
#include <cstdint>
#include <optional>

namespace llvm {
class Module;
} // namespace llvm

namespace feme {
namespace dxil {

/// One resource binding described by DXIL's `!dx.resources` metadata.
struct ResourceBinding {
  llvm::dxil::ResourceClass Class = llvm::dxil::ResourceClass::SRV;
  llvm::dxil::ResourceKind Kind = llvm::dxil::ResourceKind::Invalid;
  /// The typed buffer/texture component type, valid only when the resource's
  /// metadata carried an `ElementType` extended property.
  llvm::dxil::ElementType ElementType = llvm::dxil::ElementType::Invalid;
  uint32_t Space = 0;
  uint32_t LowerBound = 0;
  /// The number of registers in the binding range, or 0 for an unbounded
  /// range (which DXIL spells as `UINT32_MAX`).
  uint32_t RangeSize = 0;
  /// A structured buffer's element stride, or a cbuffer's size, in bytes.
  uint32_t StrideOrSize = 0;
  bool IsROV = false;
};

/// The contents of a module's `!dx.resources` metadata, keyed the way
/// `CreateHandle` refers to it: by resource class and range ID.
class ResourceMetadata {
public:
  /// Reads \p M's `!dx.resources` metadata. Resources whose metadata isn't
  /// shaped the way DXIL's writer produces are skipped rather than treated
  /// as an error, so raising can proceed on the resources it does
  /// understand.
  static ResourceMetadata read(const llvm::Module &M);

  /// Returns the binding for resource \p RangeID of class \p Class, or
  /// `std::nullopt` if the module's metadata doesn't describe one.
  std::optional<ResourceBinding> lookup(llvm::dxil::ResourceClass Class,
                                        uint32_t RangeID) const;

private:
  /// Keyed by `(class << 32) | rangeID`, which is exactly the pair
  /// `CreateHandle`'s first two operands name.
  llvm::DenseMap<uint64_t, ResourceBinding> Bindings;
};

} // namespace dxil
} // namespace feme

#endif // FEME_LIB_TRANSFORMS_DXIL_RESOURCEMETADATA_H
