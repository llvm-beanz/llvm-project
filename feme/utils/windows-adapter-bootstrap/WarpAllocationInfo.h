//===- WarpAllocationInfo.h - WARP kernel allocation ABI ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPALLOCATIONINFO_H
#define FEME_UTILS_WINDOWS_ADAPTER_BOOTSTRAP_WARPALLOCATIONINFO_H

#include <windows.h>
#include <winternl.h>

#include <d3d10umddi.h>

#include <cstddef>

namespace feme {
namespace probe {

/// Private allocation metadata supplied to the existing WARP kernel driver.
struct WarpKmdAllocationInfo {
  D3D10DDIRESOURCE_TYPE ResourceDimension;
  UINT32 Width;
  UINT32 Height;
  UINT32 Depth;
  DXGI_FORMAT Format;
  UINT32 Pitch;
  UINT32 TotalSize;
  UINT32 ArraySize;
  UINT32 MipLevels;
  UINT32 Usage;
  UINT32 BindFlags;
  DXGI_SAMPLE_DESC SampleDesc;
  D3DDDI_RATIONAL RefreshRate;
  D3DDDI_ROTATION Rotation;
  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
  union {
    UINT32 VirtualGpuAlignment;
    HANDLE FlipStart;
    UINT64 FlipStartAlignment;
  };
  union {
    struct {
      UINT32 VirtualGpuSegmentId;
      UINT32 VirtualGpuPrivateData;
    };
    HANDLE FlipFinished;
    UINT64 FlipFinishedAlignment;
  };
  UINT32 Flags;
};

static_assert(sizeof(WarpKmdAllocationInfo) == 96);
static_assert(offsetof(WarpKmdAllocationInfo, Flags) == 88);
constexpr UINT32 WarpAllocationFlagInAperture = 1u << 7;
constexpr UINT32 WarpAllocationFlagTilePool = 1u << 10;
constexpr UINT32 WarpAllocationFlagWriteCombine = 1u << 11;
constexpr UINT32 WarpAllocationFlagShared = 1u << 19;
constexpr UINT32 WarpAllocationFlagCpuVisible = 1u << 21;

} // namespace probe
} // namespace feme

#endif
