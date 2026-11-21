//===- RootBindingPlan.h - Checked DirectX root layouts ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_DXIL_ROOTBINDINGPLAN_H
#define FEME_TRANSFORMS_DXIL_ROOTBINDINGPLAN_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Support/DXILABI.h"
#include "llvm/Support/Error.h"

#include <optional>

namespace llvm {
class Function;
class Module;
class raw_ostream;
} // namespace llvm

namespace feme::dxil {

struct RootBinding {
  llvm::dxil::ResourceClass Class;
  uint32_t Space;
  uint32_t Register;
  uint32_t Count;
  uint32_t TableOffset = 0;
  uint32_t Flags = 0;
};

struct RootParameter {
  llvm::dxbc::RootParameterType Kind;
  llvm::dxbc::ShaderVisibility Visibility;
  uint32_t ConstantCount = 0;
  llvm::SmallVector<RootBinding, 1> Bindings;
};

/// Owned, read-only reflection of a serialized Root Signature 1.0/1.1.
/// APPEND offsets are resolved; flags remain the serialized promises.
/// This does not prepare descriptors or change CPU resource lowering.
class RootBindingPlan {
  uint32_t Version = 0;
  uint32_t Flags = 0;
  uint32_t DWORDCost = 0;
  llvm::SmallVector<RootParameter, 4> Parameters;
  llvm::SmallVector<llvm::dxbc::RTS0::v1::StaticSampler, 2> StaticSamplers;

  RootBindingPlan() = default;

public:
  static llvm::Expected<RootBindingPlan> parse(llvm::ArrayRef<uint8_t> Bytes);
  /// Accepts a standalone serialized root blob or a DXBC/DXIL container's
  /// RTS0 part. Missing/duplicate parts and malformed containers are errors.
  static llvm::Expected<RootBindingPlan> read(llvm::StringRef Input);

  uint32_t getVersion() const { return Version; }
  uint32_t getFlags() const { return Flags; }
  uint32_t getDWORDCost() const { return DWORDCost; }
  llvm::ArrayRef<RootParameter> getParameters() const { return Parameters; }
  llvm::ArrayRef<llvm::dxbc::RTS0::v1::StaticSampler>
  getStaticSamplers() const {
    return StaticSamplers;
  }
  /// Queries parameter bindings; static samplers are exposed separately.
  /// Stage All (compute) selects only all-stage parameters.
  const RootBinding *findBinding(llvm::dxil::ResourceClass Class,
                                 uint32_t Space, uint32_t Register,
                                 llvm::dxbc::ShaderVisibility Stage) const;
  /// Checks that one visible binding covers a complete shader declaration;
  /// arrays cannot span adjacent ranges or individual static samplers.
  /// UINT32_MAX denotes an unbounded declaration. This checks binding
  /// identity, not descriptor preparation.
  llvm::Error checkBindingCoverage(llvm::dxil::ResourceClass Class,
                                   uint32_t Space, uint32_t Register,
                                   uint32_t Count,
                                   llvm::dxbc::ShaderVisibility Stage) const;
  /// Checks live resource declarations reachable from an entry point,
  /// including direct helper calls, before normalization discards identities.
  /// Root SRV/UAV descriptors require raw/structured buffers. Native heap
  /// handles require their resource/sampler direct-index flag; emulated bound
  /// access does not. Heap contents, counters and other resource-kind/promise
  /// compatibility are separate.
  llvm::Error checkShaderBindings(const llvm::Function &Entry,
                                  llvm::dxbc::ShaderVisibility Stage) const;
  void print(llvm::raw_ostream &OS) const;
};

/// Reads canonical !feme.dxil.rootsignature metadata. Absence is distinct
/// from malformed metadata or an invalid serialized layout.
llvm::Expected<std::optional<RootBindingPlan>>
getRootBindingPlan(const llvm::Function &F);

/// After entry metadata raising, preserves a container's RTS0 bytes on its
/// shader entries. Per-entry root metadata takes precedence. Non-containers
/// and containers without RTS0 have nothing to attach; duplicate parts fail.
llvm::Error importContainerRootSignature(llvm::Module &M,
                                         llvm::StringRef Input);

} // namespace feme::dxil

#endif
