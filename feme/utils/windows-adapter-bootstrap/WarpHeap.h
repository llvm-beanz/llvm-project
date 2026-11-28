//===- WarpHeap.h - Owned resource heap DDIs ---------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPHEAP_H
#define FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPHEAP_H

#include "WarpDeviceState.h"

#ifdef FEME_BOOTSTRAP_WARP_COMPILER
#include "WarpShaderCompilerABI.h"
#include <array>
#endif

namespace feme {
namespace probe {

/// Byte geometry of the mapped, uncompressed Windows resource formats.
UINT warpFormatElementSize(DXGI_FORMAT Format);

/// Populate committed and placed resource entries, not a complete device table.
/// These callbacks use the existing WARP KMD allocation ABI. Shared-resource
/// opening, protected heaps and shader-visible texture views are not supported.
/// Publication through FillDDITable remains
/// gated on the complete device contract.
void populateWarpHeapDDIs(D3D12DDI_DEVICE_FUNCS_CORE_0072 &Table);

/// Keep live buffer mappings stable throughout a CPU command.
class WarpBufferAccess {
  WarpDeviceState *Owner;
  HRESULT storageRange(D3D12DDI_HRESOURCE Resource, UINT64 Offset, UINT64 Size,
                       void *&Data, bool UnorderedAccess, bool BufferOnly);

public:
  explicit WarpBufferAccess(WarpDeviceState *Owner);
  ~WarpBufferAccess();
  WarpBufferAccess(const WarpBufferAccess &) = delete;
  WarpBufferAccess &operator=(const WarpBufferAccess &) = delete;
  HRESULT range(D3D12DDI_HRESOURCE Resource, UINT64 Offset, UINT64 Size,
                void *&Data, bool UnorderedAccess = false);
  /// Resolve copy storage without admitting textures as shader buffers.
  HRESULT resourceRange(D3D12DDI_HRESOURCE Resource, UINT64 Offset, UINT64 Size,
                        void *&Data);
  /// Translate D3D mip/array indexing into owned physical subresource metadata.
  HRESULT subresource(
      D3D12DDI_HRESOURCE Resource, UINT Index, D3D12DDI_SUBRESOURCE_INFO &Info,
      D3D12DDIARG_PHYSICAL_SUBRESOURCE_PITCHED_LAYOUT *Layout = nullptr);
#ifdef FEME_BOOTSTRAP_WARP_COMPILER
  /// Resolve a selected SRV view without exposing texture storage as a buffer.
  HRESULT imageView(
      const D3D12DDIARG_CREATE_SHADER_RESOURCE_VIEW_0002 &View,
      FeMeWarpImageBinding &Image,
      std::array<FeMeWarpImageMipLayout, FeMeWarpMaxImageMipLevels> &Layouts);
#endif
  HRESULT address(UINT64 Address, void *&Data, UINT64 &Size,
                  bool UnorderedAccess = false);
};

/// Borrow a checked, persistently locked buffer extent for CPU commands.
HRESULT getWarpBufferRange(WarpDeviceState *Owner, D3D12DDI_HRESOURCE Resource,
                           UINT64 Offset, UINT64 Size, void *&Data,
                           bool UnorderedAccess = false);
HRESULT getWarpBufferAddress(WarpDeviceState *Owner, UINT64 Address,
                             void *&Data, UINT64 &Size,
                             bool UnorderedAccess = false);

} // namespace probe
} // namespace feme

#endif
