//===- DXILResource.h - DXIL Resource helper objects ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains helper objects for working with DXIL Resources.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TARGET_DIRECTX_DXILRESOURCE_H
#define LLVM_TARGET_DIRECTX_DXILRESOURCE_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include <cstdint>

namespace llvm {
class Module;
class Metadata;
class GlobalVariable;

namespace dxil {

class ResourceBase {
protected:
  uint32_t ID;
  GlobalVariable *GV;
  StringRef Name;
  uint32_t Space;
  uint32_t LowerBound;
  uint32_t RangeSize;
  ResourceBase(uint32_t I, GlobalVariable *V);

  enum Kinds : uint32_t {
    Invalid = 0,
    Texture1D,
    Texture2D,
    Texture2DMS,
    Texture3D,
    TextureCube,
    Texture1DArray,
    Texture2DArray,
    Texture2DMSArray,
    TextureCubeArray,
    TypedBuffer,
    RawBuffer,
    StructuredBuffer,
    CBuffer,
    Sampler,
    TBuffer,
    RTAccelerationStructure,
    FeedbackTexture2D,
    FeedbackTexture2DArray,
  }

public:
  struct ExtendedProperties {
    llvm::Optional<uint32_t> ElementType;
    llvm::Optional<uint32_t> ElementStride;
    llvm::Optional<uint32_t> FeedbackKind;
    llvm::Optional<bool> Uses64BitAtomics;

    enum Tags : uint32_t {
      TypedBufferElementType = 0,
      StructuredBufferElementStride,
      SamplerFeedbackKind,
      Atomic64Use
    };
  };
};

class UAVResource : public ResourceBase {
  uint32_t Shape;
  bool GloballyCoherent;
  bool HasCounter;
  bool IsROV;
  ResourceBase::ExtendedProperties ExtProps;

  void parseSourceType(StringRef S);

  UAVResource(uint32_t I, GlobalVariable *V) : ResourceBase(I, V) {}
public:
  static UAVResource fromFrontendMetadata(Metadata *MD);
}

class Resources {
  Module &Mod;
  llvm::SmallVector<UAVResource> UAVs;

  void collectUAVs();
public:
  Resources(Module &M) : Mod(M) { collectUAVs(); }
};



} // namespace dxil
} // namespace llvm

#endif // LLVM_TARGET_DIRECTX_DXILRESOURCE_H
