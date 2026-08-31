//===- ImageFixture.cpp - Textual image fixture read/write --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Two encodings are used for one texel, chosen per format (see
// "getFormatInfo"'s own comment): a single concatenated hex integer -- one
// zero-padded chunk of `ComponentBytes * 2` hex digits per component, most
// significant component first, matching feme/docs/Design.md's own
// `r8g8b8a8-unorm` example -- for integer/normalized formats, or a
// comma-joined, per-component fixed-precision decimal for floating-point
// ones (a single decimal token cannot hold more than one component, so a
// multi-component float texel is still exactly one whitespace-delimited
// token, just an internally-delimited one). Each hex/decimal component is
// the component's raw bit pattern / value, stored into (or read from) the
// image's backing bytes in the host's native layout -- the same layout
// `feme::cpu::FemeImageDescriptor::Data` already uses -- so a parsed
// fixture is usable directly as image heap storage.
//
//===----------------------------------------------------------------------===//

#include "feme/Graphics/ImageFixture.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace llvm;
using namespace feme::cpu;

namespace feme::graphics {

namespace {

/// The component count/width and encoding (hex vs. decimal) one
/// `ResourceFormat` uses in a fixture. Only the formats
/// `runtime/CPU/FeMeRuntimeCPU.c`'s image helpers and feme-run's own
/// heap-image support already cover are implemented; every other format is
/// a mechanical, on-demand addition to this switch (the same "Additional
/// formats extend one helper implementation" pattern
/// feme/docs/FeMeCPUDesign.md's "Descriptor formats" already establishes),
/// not a fixture-format limitation.
struct FormatInfo {
  uint32_t Components = 0;
  uint32_t ComponentBytes = 0;
  bool IsFloat = false;
};

Expected<FormatInfo> getFormatInfo(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::R32_FLOAT:
    return FormatInfo{1, 4, true};
  case ResourceFormat::R32G32_FLOAT:
    return FormatInfo{2, 4, true};
  case ResourceFormat::R32G32B32_FLOAT:
    return FormatInfo{3, 4, true};
  case ResourceFormat::R32G32B32A32_FLOAT:
    return FormatInfo{4, 4, true};
  case ResourceFormat::R32_UINT:
  case ResourceFormat::R32_SINT:
    return FormatInfo{1, 4, false};
  case ResourceFormat::R32G32_UINT:
  case ResourceFormat::R32G32_SINT:
    return FormatInfo{2, 4, false};
  case ResourceFormat::R32G32B32_UINT:
  case ResourceFormat::R32G32B32_SINT:
    return FormatInfo{3, 4, false};
  case ResourceFormat::R32G32B32A32_UINT:
  case ResourceFormat::R32G32B32A32_SINT:
    return FormatInfo{4, 4, false};
  case ResourceFormat::R8G8B8A8_UNORM:
  case ResourceFormat::R8G8B8A8_SNORM:
  case ResourceFormat::R8G8B8A8_UINT:
  case ResourceFormat::R8G8B8A8_SINT:
  case ResourceFormat::R8G8B8A8_UNORM_SRGB:
    return FormatInfo{4, 1, false};
  case ResourceFormat::B8G8R8A8_UNORM:
    // Same 4-byte-per-texel, 1-byte-per-component layout as
    // `R8G8B8A8_UNORM`, just with the red and blue components swapped in
    // memory (`packClearColor`/`unpackColor` handle the swap; roadmap C1).
    return FormatInfo{4, 1, false};
  case ResourceFormat::R16G16B16A16_FLOAT:
    return FormatInfo{4, 2, true};
  case ResourceFormat::R16G16B16A16_UNORM:
  case ResourceFormat::R16G16B16A16_SNORM:
  case ResourceFormat::R16G16B16A16_UINT:
  case ResourceFormat::R16G16B16A16_SINT:
    return FormatInfo{4, 2, false};
  case ResourceFormat::R11G11B10_FLOAT:
    // Packed into a single 4-byte word; not a per-component layout, so it
    // is described as a single opaque 4-byte "component" here (see
    // `packClearColor`/`decodeAttribute`'s own dedicated packing code for
    // this format).
    return FormatInfo{1, 4, false};
  case ResourceFormat::R10G10B10A2_UNORM:
  case ResourceFormat::R10G10B10A2_UINT:
    return FormatInfo{1, 4, false};
  case ResourceFormat::D16_UNORM:
    return FormatInfo{1, 2, false};
  case ResourceFormat::D32_FLOAT:
    return FormatInfo{1, 4, true};
  case ResourceFormat::D24_UNORM_S8_UINT:
    // Depth (24 bits, stored in the low 24 bits of a 32-bit word) and
    // stencil (8 bits, the high byte) packed into one 4-byte word.
    return FormatInfo{1, 4, false};
  case ResourceFormat::D32_FLOAT_S8X24_UINT:
    // Depth (32-bit float) and stencil (8 bits, in the low byte of the
    // second word) stored as two 4-byte words.
    return FormatInfo{2, 4, false};
  case ResourceFormat::S8_UINT:
    return FormatInfo{1, 1, false};
  case ResourceFormat::A8_UNORM:
    // A single 8-bit alpha component (roadmap E5); still described as a
    // 4-logical-component clear color by `packClearColor`/`unpackColor`
    // below, exactly like `R10G10B10A2_UNORM`'s own opaque-word case.
    return FormatInfo{1, 1, false};
  case ResourceFormat::A1B5G5R5_UNORM:
    // Packed into a single 2-byte word (roadmap E5); not a per-component
    // layout, so it is described as one opaque 2-byte "component" here,
    // the same convention `R10G10B10A2_UNORM` above uses.
    return FormatInfo{1, 2, false};
  case ResourceFormat::R4G4B4A4_UNORM:
  case ResourceFormat::B4G4R4A4_UNORM:
  case ResourceFormat::R5G6B5_UNORM:
  case ResourceFormat::B5G6R5_UNORM:
  case ResourceFormat::R5G5B5A1_UNORM:
  case ResourceFormat::B5G5R5A1_UNORM:
  case ResourceFormat::A1R5G5B5_UNORM:
    // (Roadmap H7r) The remaining core-1.0 packed 16-bit formats: also a
    // single opaque 2-byte word each, the same convention as
    // `A1B5G5R5_UNORM` immediately above.
    return FormatInfo{1, 2, false};
  default:
    return createStringError(inconvertibleErrorCode(),
                             "image fixture format is not yet supported "
                             "(mechanical, added on demand -- see 'Texture "
                             "layout and formats' in "
                             "feme/docs/FeMeGraphicsDesign.md)");
  }
}

} // namespace

/// Parses a fixture's `format` field, the hyphen-separated spelling
/// feme/docs/Design.md's own example uses (`r8g8b8a8-unorm`) -- distinct
/// from feme-run's underscore-separated heap YAML spelling, since each is
/// literally what its own spec already shows.
Expected<ResourceFormat> parseFixtureFormat(StringRef Format) {
  ResourceFormat Result =
      StringSwitch<ResourceFormat>(Format)
          .Case("r32-float", ResourceFormat::R32_FLOAT)
          .Case("r32g32-float", ResourceFormat::R32G32_FLOAT)
          .Case("r32g32b32-float", ResourceFormat::R32G32B32_FLOAT)
          .Case("r32g32b32a32-float", ResourceFormat::R32G32B32A32_FLOAT)
          .Case("r32-uint", ResourceFormat::R32_UINT)
          .Case("r32g32-uint", ResourceFormat::R32G32_UINT)
          .Case("r32g32b32-uint", ResourceFormat::R32G32B32_UINT)
          .Case("r32g32b32a32-uint", ResourceFormat::R32G32B32A32_UINT)
          .Case("r32-sint", ResourceFormat::R32_SINT)
          .Case("r32g32-sint", ResourceFormat::R32G32_SINT)
          .Case("r32g32b32-sint", ResourceFormat::R32G32B32_SINT)
          .Case("r32g32b32a32-sint", ResourceFormat::R32G32B32A32_SINT)
          .Case("r8g8b8a8-unorm", ResourceFormat::R8G8B8A8_UNORM)
          .Case("r8g8b8a8-snorm", ResourceFormat::R8G8B8A8_SNORM)
          .Case("r8g8b8a8-uint", ResourceFormat::R8G8B8A8_UINT)
          .Case("r8g8b8a8-sint", ResourceFormat::R8G8B8A8_SINT)
          .Case("r8g8b8a8-unorm-srgb", ResourceFormat::R8G8B8A8_UNORM_SRGB)
          .Case("b8g8r8a8-unorm", ResourceFormat::B8G8R8A8_UNORM)
          .Case("r16g16b16a16-float", ResourceFormat::R16G16B16A16_FLOAT)
          .Case("r16g16b16a16-unorm", ResourceFormat::R16G16B16A16_UNORM)
          .Case("r16g16b16a16-snorm", ResourceFormat::R16G16B16A16_SNORM)
          .Case("r16g16b16a16-uint", ResourceFormat::R16G16B16A16_UINT)
          .Case("r16g16b16a16-sint", ResourceFormat::R16G16B16A16_SINT)
          .Case("r11g11b10-float", ResourceFormat::R11G11B10_FLOAT)
          .Case("r10g10b10a2-unorm", ResourceFormat::R10G10B10A2_UNORM)
          .Case("r10g10b10a2-uint", ResourceFormat::R10G10B10A2_UINT)
          .Case("d16-unorm", ResourceFormat::D16_UNORM)
          .Case("d32-float", ResourceFormat::D32_FLOAT)
          .Case("d24-unorm-s8-uint", ResourceFormat::D24_UNORM_S8_UINT)
          .Case("d32-float-s8x24-uint", ResourceFormat::D32_FLOAT_S8X24_UINT)
          .Case("s8-uint", ResourceFormat::S8_UINT)
          .Case("a8-unorm", ResourceFormat::A8_UNORM)
          .Case("a1b5g5r5-unorm", ResourceFormat::A1B5G5R5_UNORM)
          .Case("a4r4g4b4-unorm", ResourceFormat::A4R4G4B4_UNORM)
          .Case("a4b4g4r4-unorm", ResourceFormat::A4B4G4R4_UNORM)
          .Case("r4g4b4a4-unorm", ResourceFormat::R4G4B4A4_UNORM)
          .Case("b4g4r4a4-unorm", ResourceFormat::B4G4R4A4_UNORM)
          .Case("r5g6b5-unorm", ResourceFormat::R5G6B5_UNORM)
          .Case("b5g6r5-unorm", ResourceFormat::B5G6R5_UNORM)
          .Case("r5g5b5a1-unorm", ResourceFormat::R5G5B5A1_UNORM)
          .Case("b5g5r5a1-unorm", ResourceFormat::B5G5R5A1_UNORM)
          .Case("a1r5g5b5-unorm", ResourceFormat::A1R5G5B5_UNORM)
          .Case("r8-unorm", ResourceFormat::R8_UNORM)
          .Case("r8-snorm", ResourceFormat::R8_SNORM)
          .Case("r8-uint", ResourceFormat::R8_UINT)
          .Case("r8-sint", ResourceFormat::R8_SINT)
          .Case("r8g8-unorm", ResourceFormat::R8G8_UNORM)
          .Case("r8g8-snorm", ResourceFormat::R8G8_SNORM)
          .Case("r8g8-uint", ResourceFormat::R8G8_UINT)
          .Case("r8g8-sint", ResourceFormat::R8G8_SINT)
          .Default(ResourceFormat::Unknown);
  if (Result == ResourceFormat::Unknown)
    return createStringError(inconvertibleErrorCode(),
                             "unknown image fixture format '%s'",
                             Format.str().c_str());
  return Result;
}

Expected<uint32_t> getFixtureFormatElementSize(ResourceFormat Format) {
  Expected<FormatInfo> Info = getFormatInfo(Format);
  if (!Info)
    return Info.takeError();
  return Info->Components * Info->ComponentBytes;
}

Expected<bool> isFixtureFormatFloat(ResourceFormat Format) {
  Expected<FormatInfo> Info = getFormatInfo(Format);
  if (!Info)
    return Info.takeError();
  return Info->IsFloat;
}

Error packClearColor(ResourceFormat Format, ArrayRef<double> Clear,
                     MutableArrayRef<uint8_t> Texel) {
  // `R10G10B10A2_UNORM` is a single packed 32-bit word in the fixture/
  // clear-color table (`getFormatInfo`'s `Components == 1`, matching its
  // opaque hex-text representation), but a clear color is still four
  // logical components; special-cased here rather than forced through
  // `getFormatInfo`'s generic per-component loop.
  if (Format == ResourceFormat::R10G10B10A2_UNORM) {
    if (Clear.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "clear color has %zu component(s), expected 4",
                               Clear.size());
    auto Norm = [](double V) -> uint32_t {
      return static_cast<uint32_t>(std::lround(std::clamp(V, 0.0, 1.0) * 1023.0));
    };
    // VK_FORMAT_A2B10G10R10_UNORM_PACK32: from the MSB down, 2 bits of A,
    // 10 bits of B, 10 bits of G, 10 bits of R.
    uint32_t Word = (static_cast<uint32_t>(std::lround(
                         std::clamp(Clear[3], 0.0, 1.0) * 3.0))
                     << 30) |
                    (Norm(Clear[2]) << 20) | (Norm(Clear[1]) << 10) |
                    Norm(Clear[0]);
    memcpy(Texel.data(), &Word, sizeof(Word));
    return Error::success();
  }

  // (Roadmap E5) `VK_FORMAT_A1B5G5R5_UNORM_PACK16`: the same
  // single-packed-word special case as `R10G10B10A2_UNORM` above, just
  // 16 bits wide with alpha at the MSB rather than the LSB.
  if (Format == ResourceFormat::A1B5G5R5_UNORM) {
    if (Clear.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "clear color has %zu component(s), expected 4",
                               Clear.size());
    auto Norm5 = [](double V) -> uint16_t {
      return static_cast<uint16_t>(std::lround(std::clamp(V, 0.0, 1.0) * 31.0));
    };
    // From the MSB down: 1 bit of A, 5 bits of B, 5 bits of G, 5 bits of R.
    uint16_t Word = static_cast<uint16_t>(
        (static_cast<uint16_t>(std::lround(std::clamp(Clear[3], 0.0, 1.0)))
         << 15) |
        (Norm5(Clear[2]) << 10) | (Norm5(Clear[1]) << 5) | Norm5(Clear[0]));
    memcpy(Texel.data(), &Word, sizeof(Word));
    return Error::success();
  }

  // (Roadmap H7r) The remaining core-1.0 packed 16-bit formats: the same
  // single-packed-word special case as `A1B5G5R5_UNORM` above, just with
  // each format's own bit widths/ordering (see the Vulkan spec's own
  // "Packed Formats" section) and, for the two 3-component formats below
  // (`R5G6B5_UNORM`/`B5G6R5_UNORM`), a `Clear` alpha component that is
  // simply ignored -- neither format has any bits to store it in.
  if (Format == ResourceFormat::R4G4B4A4_UNORM ||
      Format == ResourceFormat::B4G4R4A4_UNORM) {
    if (Clear.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "clear color has %zu component(s), expected 4",
                               Clear.size());
    auto Norm4 = [](double V) -> uint16_t {
      return static_cast<uint16_t>(std::lround(std::clamp(V, 0.0, 1.0) * 15.0));
    };
    // R4G4B4A4: R[15:12] G[11:8] B[7:4] A[3:0]. B4G4R4A4: the same, with
    // R and B swapped.
    unsigned FirstIdx = Format == ResourceFormat::R4G4B4A4_UNORM ? 0 : 2;
    unsigned ThirdIdx = Format == ResourceFormat::R4G4B4A4_UNORM ? 2 : 0;
    uint16_t Word = static_cast<uint16_t>(
        (Norm4(Clear[FirstIdx]) << 12) | (Norm4(Clear[1]) << 8) |
        (Norm4(Clear[ThirdIdx]) << 4) | Norm4(Clear[3]));
    memcpy(Texel.data(), &Word, sizeof(Word));
    return Error::success();
  }
  if (Format == ResourceFormat::R5G6B5_UNORM ||
      Format == ResourceFormat::B5G6R5_UNORM) {
    if (Clear.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "clear color has %zu component(s), expected 4",
                               Clear.size());
    auto Norm5 = [](double V) -> uint16_t {
      return static_cast<uint16_t>(std::lround(std::clamp(V, 0.0, 1.0) * 31.0));
    };
    auto Norm6 = [](double V) -> uint16_t {
      return static_cast<uint16_t>(std::lround(std::clamp(V, 0.0, 1.0) * 63.0));
    };
    // R5G6B5: R[15:11] G[10:5] B[4:0]. B5G6R5: the same, with R and B
    // swapped. Neither has an alpha bit -- `Clear[3]` is ignored.
    unsigned FirstIdx = Format == ResourceFormat::R5G6B5_UNORM ? 0 : 2;
    unsigned ThirdIdx = Format == ResourceFormat::R5G6B5_UNORM ? 2 : 0;
    uint16_t Word = static_cast<uint16_t>((Norm5(Clear[FirstIdx]) << 11) |
                                          (Norm6(Clear[1]) << 5) |
                                          Norm5(Clear[ThirdIdx]));
    memcpy(Texel.data(), &Word, sizeof(Word));
    return Error::success();
  }
  if (Format == ResourceFormat::R5G5B5A1_UNORM ||
      Format == ResourceFormat::B5G5R5A1_UNORM) {
    if (Clear.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "clear color has %zu component(s), expected 4",
                               Clear.size());
    auto Norm5 = [](double V) -> uint16_t {
      return static_cast<uint16_t>(std::lround(std::clamp(V, 0.0, 1.0) * 31.0));
    };
    // R5G5B5A1: R[15:11] G[10:6] B[5:1] A[0]. B5G5R5A1: the same, with R
    // and B swapped.
    unsigned FirstIdx = Format == ResourceFormat::R5G5B5A1_UNORM ? 0 : 2;
    unsigned ThirdIdx = Format == ResourceFormat::R5G5B5A1_UNORM ? 2 : 0;
    uint16_t Word = static_cast<uint16_t>(
        (Norm5(Clear[FirstIdx]) << 11) | (Norm5(Clear[1]) << 6) |
        (Norm5(Clear[ThirdIdx]) << 1) |
        static_cast<uint16_t>(std::lround(std::clamp(Clear[3], 0.0, 1.0))));
    memcpy(Texel.data(), &Word, sizeof(Word));
    return Error::success();
  }
  if (Format == ResourceFormat::A1R5G5B5_UNORM) {
    if (Clear.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "clear color has %zu component(s), expected 4",
                               Clear.size());
    auto Norm5 = [](double V) -> uint16_t {
      return static_cast<uint16_t>(std::lround(std::clamp(V, 0.0, 1.0) * 31.0));
    };
    // A1R5G5B5: A[15] R[14:10] G[9:5] B[4:0].
    uint16_t Word = static_cast<uint16_t>(
        (static_cast<uint16_t>(std::lround(std::clamp(Clear[3], 0.0, 1.0)))
         << 15) |
        (Norm5(Clear[0]) << 10) | (Norm5(Clear[1]) << 5) | Norm5(Clear[2]));
    memcpy(Texel.data(), &Word, sizeof(Word));
    return Error::success();
  }

  // (Roadmap E5) `VK_FORMAT_A8_UNORM`: a single alpha byte -- the clear
  // color's other three (unused) components are simply ignored.
  if (Format == ResourceFormat::A8_UNORM) {
    if (Clear.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "clear color has %zu component(s), expected 4",
                               Clear.size());
    Texel[0] = static_cast<uint8_t>(
        std::lround(std::clamp(Clear[3], 0.0, 1.0) * 255.0));
    return Error::success();
  }

  Expected<FormatInfo> Info = getFormatInfo(Format);
  if (!Info)
    return Info.takeError();
  if (Clear.size() != Info->Components)
    return createStringError(inconvertibleErrorCode(),
                             "clear color has %zu component(s), expected %u",
                             Clear.size(), Info->Components);

  if (Info->IsFloat) {
    for (unsigned I = 0; I != Info->Components; ++I) {
      float F = static_cast<float>(Clear[I]);
      memcpy(Texel.data() + I * Info->ComponentBytes, &F, Info->ComponentBytes);
    }
    return Error::success();
  }

  if (Format == ResourceFormat::R8G8B8A8_UNORM ||
      Format == ResourceFormat::R8G8B8A8_UNORM_SRGB) {
    for (unsigned I = 0; I != Info->Components; ++I) {
      double Clamped = std::clamp(Clear[I], 0.0, 1.0);
      Texel[I] = static_cast<uint8_t>(std::lround(Clamped * 255.0));
    }
    return Error::success();
  }

  if (Format == ResourceFormat::B8G8R8A8_UNORM) {
    // Same encoding as `R8G8B8A8_UNORM`, but memory order is B, G, R, A:
    // `Clear` is always logical [R, G, B, A] (matching
    // `VkClearColorValue::float32`).
    static const unsigned Swizzle[4] = {2, 1, 0, 3};
    for (unsigned I = 0; I != Info->Components; ++I) {
      double Clamped = std::clamp(Clear[Swizzle[I]], 0.0, 1.0);
      Texel[I] = static_cast<uint8_t>(std::lround(Clamped * 255.0));
    }
    return Error::success();
  }

  if (Format == ResourceFormat::R16G16B16A16_UNORM) {
    for (unsigned I = 0; I != Info->Components; ++I) {
      double Clamped = std::clamp(Clear[I], 0.0, 1.0);
      uint16_t V = static_cast<uint16_t>(std::lround(Clamped * 65535.0));
      memcpy(Texel.data() + I * 2, &V, 2);
    }
    return Error::success();
  }

  if (Format == ResourceFormat::R16G16B16A16_SNORM) {
    for (unsigned I = 0; I != Info->Components; ++I) {
      double Clamped = std::clamp(Clear[I], -1.0, 1.0);
      int16_t V = static_cast<int16_t>(std::lround(Clamped * 32767.0));
      memcpy(Texel.data() + I * 2, &V, 2);
    }
    return Error::success();
  }

  if (Format == ResourceFormat::D16_UNORM) {
    double Clamped = std::clamp(Clear[0], 0.0, 1.0);
    uint16_t V = static_cast<uint16_t>(std::lround(Clamped * 65535.0));
    memcpy(Texel.data(), &V, sizeof(V));
    return Error::success();
  }

  if (Format == ResourceFormat::S8_UINT) {
    // Stencil clear values are an integer reference value, not a
    // normalized fraction (matching `VkClearDepthStencilValue::stencil`).
    Texel[0] = static_cast<uint8_t>(std::clamp(Clear[0], 0.0, 255.0));
    return Error::success();
  }

  return createStringError(inconvertibleErrorCode(),
                           "attachment clear color is not yet supported "
                           "for this format");
}

Error unpackColor(ResourceFormat Format, ArrayRef<uint8_t> Texel,
                  MutableArrayRef<double> Out) {
  if (Format == ResourceFormat::R10G10B10A2_UNORM) {
    if (Out.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "unpack destination has %zu component(s), "
                               "expected 4",
                               Out.size());
    uint32_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    Out[0] = (Word & 0x3FF) / 1023.0;
    Out[1] = ((Word >> 10) & 0x3FF) / 1023.0;
    Out[2] = ((Word >> 20) & 0x3FF) / 1023.0;
    Out[3] = ((Word >> 30) & 0x3) / 3.0;
    return Error::success();
  }

  // (Roadmap E5) `VK_FORMAT_A1B5G5R5_UNORM_PACK16`: the inverse of
  // `packClearColor`'s special case above.
  if (Format == ResourceFormat::A1B5G5R5_UNORM) {
    if (Out.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "unpack destination has %zu component(s), "
                               "expected 4",
                               Out.size());
    uint16_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    Out[0] = (Word & 0x1F) / 31.0;
    Out[1] = ((Word >> 5) & 0x1F) / 31.0;
    Out[2] = ((Word >> 10) & 0x1F) / 31.0;
    Out[3] = ((Word >> 15) & 0x1) / 1.0;
    return Error::success();
  }

  // (Roadmap H7r) The remaining core-1.0 packed 16-bit formats: the
  // inverse of `packClearColor`'s special cases above.
  if (Format == ResourceFormat::R4G4B4A4_UNORM ||
      Format == ResourceFormat::B4G4R4A4_UNORM) {
    if (Out.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "unpack destination has %zu component(s), "
                               "expected 4",
                               Out.size());
    uint16_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    unsigned FirstIdx = Format == ResourceFormat::R4G4B4A4_UNORM ? 0 : 2;
    unsigned ThirdIdx = Format == ResourceFormat::R4G4B4A4_UNORM ? 2 : 0;
    Out[FirstIdx] = ((Word >> 12) & 0xF) / 15.0;
    Out[1] = ((Word >> 8) & 0xF) / 15.0;
    Out[ThirdIdx] = ((Word >> 4) & 0xF) / 15.0;
    Out[3] = (Word & 0xF) / 15.0;
    return Error::success();
  }
  if (Format == ResourceFormat::R5G6B5_UNORM ||
      Format == ResourceFormat::B5G6R5_UNORM) {
    if (Out.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "unpack destination has %zu component(s), "
                               "expected 4",
                               Out.size());
    uint16_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    unsigned FirstIdx = Format == ResourceFormat::R5G6B5_UNORM ? 0 : 2;
    unsigned ThirdIdx = Format == ResourceFormat::R5G6B5_UNORM ? 2 : 0;
    Out[FirstIdx] = ((Word >> 11) & 0x1F) / 31.0;
    Out[1] = ((Word >> 5) & 0x3F) / 63.0;
    Out[ThirdIdx] = (Word & 0x1F) / 31.0;
    // Neither format has an alpha channel -- read back as opaque, matching
    // `A8_UNORM`'s own "missing channel reads as its identity value" style
    // precedent below (there `0`, here `1.0` since color reads default to
    // fully opaque).
    Out[3] = 1.0;
    return Error::success();
  }
  if (Format == ResourceFormat::R5G5B5A1_UNORM ||
      Format == ResourceFormat::B5G5R5A1_UNORM) {
    if (Out.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "unpack destination has %zu component(s), "
                               "expected 4",
                               Out.size());
    uint16_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    unsigned FirstIdx = Format == ResourceFormat::R5G5B5A1_UNORM ? 0 : 2;
    unsigned ThirdIdx = Format == ResourceFormat::R5G5B5A1_UNORM ? 2 : 0;
    Out[FirstIdx] = ((Word >> 11) & 0x1F) / 31.0;
    Out[1] = ((Word >> 6) & 0x1F) / 31.0;
    Out[ThirdIdx] = ((Word >> 1) & 0x1F) / 31.0;
    Out[3] = (Word & 0x1) / 1.0;
    return Error::success();
  }
  if (Format == ResourceFormat::A1R5G5B5_UNORM) {
    if (Out.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "unpack destination has %zu component(s), "
                               "expected 4",
                               Out.size());
    uint16_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    Out[0] = ((Word >> 10) & 0x1F) / 31.0;
    Out[1] = ((Word >> 5) & 0x1F) / 31.0;
    Out[2] = (Word & 0x1F) / 31.0;
    Out[3] = ((Word >> 15) & 0x1) / 1.0;
    return Error::success();
  }

  // (Roadmap E5) `VK_FORMAT_A8_UNORM`: the inverse of `packClearColor`'s
  // special case above -- the color components read back as `0`, matching
  // this format's lack of any.
  if (Format == ResourceFormat::A8_UNORM) {
    if (Out.size() != 4)
      return createStringError(inconvertibleErrorCode(),
                               "unpack destination has %zu component(s), "
                               "expected 4",
                               Out.size());
    Out[0] = Out[1] = Out[2] = 0.0;
    Out[3] = Texel[0] / 255.0;
    return Error::success();
  }

  Expected<FormatInfo> Info = getFormatInfo(Format);
  if (!Info)
    return Info.takeError();
  if (Out.size() != Info->Components)
    return createStringError(inconvertibleErrorCode(),
                             "unpack destination has %zu component(s), "
                             "expected %u",
                             Out.size(), Info->Components);

  if (Info->IsFloat) {
    for (unsigned I = 0; I != Info->Components; ++I) {
      float F;
      memcpy(&F, Texel.data() + I * Info->ComponentBytes, Info->ComponentBytes);
      Out[I] = F;
    }
    return Error::success();
  }

  if (Format == ResourceFormat::R8G8B8A8_UNORM ||
      Format == ResourceFormat::R8G8B8A8_UNORM_SRGB) {
    for (unsigned I = 0; I != Info->Components; ++I)
      Out[I] = Texel[I] / 255.0;
    return Error::success();
  }

  if (Format == ResourceFormat::B8G8R8A8_UNORM) {
    static const unsigned Swizzle[4] = {2, 1, 0, 3};
    for (unsigned I = 0; I != Info->Components; ++I)
      Out[Swizzle[I]] = Texel[I] / 255.0;
    return Error::success();
  }

  if (Format == ResourceFormat::R16G16B16A16_UNORM) {
    for (unsigned I = 0; I != Info->Components; ++I) {
      uint16_t V;
      memcpy(&V, Texel.data() + I * 2, 2);
      Out[I] = V / 65535.0;
    }
    return Error::success();
  }

  if (Format == ResourceFormat::R16G16B16A16_SNORM) {
    for (unsigned I = 0; I != Info->Components; ++I) {
      int16_t V;
      memcpy(&V, Texel.data() + I * 2, 2);
      Out[I] = std::clamp(V / 32767.0, -1.0, 1.0);
    }
    return Error::success();
  }

  return createStringError(inconvertibleErrorCode(),
                           "attachment color is not yet unpackable for "
                           "this format");
}

Error packDepthClear(ResourceFormat Format, double Depth,
                     MutableArrayRef<uint8_t> Texel) {
  double Clamped = std::clamp(Depth, 0.0, 1.0);
  switch (Format) {
  case ResourceFormat::D16_UNORM: {
    uint16_t V = static_cast<uint16_t>(std::lround(Clamped * 65535.0));
    memcpy(Texel.data(), &V, sizeof(V));
    return Error::success();
  }
  case ResourceFormat::D32_FLOAT: {
    float F = static_cast<float>(Clamped);
    memcpy(Texel.data(), &F, sizeof(F));
    return Error::success();
  }
  case ResourceFormat::D24_UNORM_S8_UINT: {
    // Read-modify-write: only the low 24 bits are depth, and the high
    // byte (stencil) must survive untouched (see this function's header
    // comment).
    uint32_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    uint32_t D = static_cast<uint32_t>(std::lround(Clamped * 16777215.0));
    Word = (Word & 0xFF000000u) | (D & 0x00FFFFFFu);
    memcpy(Texel.data(), &Word, sizeof(Word));
    return Error::success();
  }
  case ResourceFormat::D32_FLOAT_S8X24_UINT: {
    // Unlike `D24_UNORM_S8_UINT`, depth and stencil are two entirely
    // separate 4-byte words (`getFormatInfo`'s own comment), not bits of
    // one shared word, so this is a plain write of the first word rather
    // than a read-modify-write.
    float F = static_cast<float>(Clamped);
    memcpy(Texel.data(), &F, sizeof(F));
    return Error::success();
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "depth clear is not yet supported for this "
                             "format");
  }
}

Error unpackDepth(ResourceFormat Format, ArrayRef<uint8_t> Texel,
                  double &Depth) {
  switch (Format) {
  case ResourceFormat::D16_UNORM: {
    uint16_t V;
    memcpy(&V, Texel.data(), sizeof(V));
    Depth = V / 65535.0;
    return Error::success();
  }
  case ResourceFormat::D32_FLOAT: {
    float F;
    memcpy(&F, Texel.data(), sizeof(F));
    Depth = F;
    return Error::success();
  }
  case ResourceFormat::D24_UNORM_S8_UINT: {
    uint32_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    Depth = (Word & 0x00FFFFFFu) / 16777215.0;
    return Error::success();
  }
  case ResourceFormat::D32_FLOAT_S8X24_UINT: {
    float F;
    memcpy(&F, Texel.data(), sizeof(F));
    Depth = F;
    return Error::success();
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "depth unpack is not yet supported for this "
                             "format");
  }
}

Error packStencilClear(ResourceFormat Format, uint32_t Stencil,
                       MutableArrayRef<uint8_t> Texel) {
  // Stencil clear/reference values are an integer, not a normalized
  // fraction (matching `VkClearDepthStencilValue::stencil`).
  uint8_t S = static_cast<uint8_t>(std::min<uint32_t>(Stencil, 0xFF));
  switch (Format) {
  case ResourceFormat::S8_UINT:
    Texel[0] = S;
    return Error::success();
  case ResourceFormat::D24_UNORM_S8_UINT: {
    // Read-modify-write: only the high byte is stencil; the low 24 bits
    // (depth) must survive untouched.
    uint32_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    Word = (Word & 0x00FFFFFFu) | (static_cast<uint32_t>(S) << 24);
    memcpy(Texel.data(), &Word, sizeof(Word));
    return Error::success();
  }
  case ResourceFormat::D32_FLOAT_S8X24_UINT: {
    // Stencil lives in the low byte of the second 4-byte word
    // (`getFormatInfo`'s own comment); a read-modify-write of that word
    // preserves its own upper, otherwise-unused bytes.
    uint32_t Word;
    memcpy(&Word, Texel.data() + 4, sizeof(Word));
    Word = (Word & 0xFFFFFF00u) | static_cast<uint32_t>(S);
    memcpy(Texel.data() + 4, &Word, sizeof(Word));
    return Error::success();
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "stencil clear is not yet supported for this "
                             "format");
  }
}

Error unpackStencil(ResourceFormat Format, ArrayRef<uint8_t> Texel,
                    uint32_t &Stencil) {
  switch (Format) {
  case ResourceFormat::S8_UINT:
    Stencil = Texel[0];
    return Error::success();
  case ResourceFormat::D24_UNORM_S8_UINT: {
    uint32_t Word;
    memcpy(&Word, Texel.data(), sizeof(Word));
    Stencil = Word >> 24;
    return Error::success();
  }
  case ResourceFormat::D32_FLOAT_S8X24_UINT: {
    uint32_t Word;
    memcpy(&Word, Texel.data() + 4, sizeof(Word));
    Stencil = Word & 0xFFu;
    return Error::success();
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "stencil unpack is not yet supported for this "
                             "format");
  }
}

namespace {

/// Copies one texel's depth aspect between \p Buffer (\p Format's own
/// per-texel depth-aspect encoding, `getDepthAspectBufferSize`) and
/// \p ImageTexel (a full, interleaved combined depth/stencil texel), in
/// whichever direction \p ToImage selects, as a read-modify-write that
/// leaves the texel's stencil bits untouched on a buffer-to-image copy.
/// Shared per-texel body of `copyDepthAspectRegion`'s loop below.
Error copyOneDepthAspectTexel(ResourceFormat Format, bool ToImage,
                              MutableArrayRef<uint8_t> Buffer,
                              MutableArrayRef<uint8_t> ImageTexel) {
  switch (Format) {
  case ResourceFormat::D24_UNORM_S8_UINT: {
    // Both the buffer's depth-aspect word and the image's combined texel
    // use the same 32-bit-word layout (D24 in the low 24 bits, Vulkan
    // spec "Buffer and Image Addressing"); only the stencil aspect's high
    // byte must be preserved on a buffer-to-image write.
    uint32_t BufferWord;
    memcpy(&BufferWord, Buffer.data(), sizeof(BufferWord));
    if (ToImage) {
      uint32_t ImageWord;
      memcpy(&ImageWord, ImageTexel.data(), sizeof(ImageWord));
      ImageWord = (ImageWord & 0xFF000000u) | (BufferWord & 0x00FFFFFFu);
      memcpy(ImageTexel.data(), &ImageWord, sizeof(ImageWord));
    } else {
      memcpy(Buffer.data(), ImageTexel.data(), sizeof(uint32_t));
    }
    return Error::success();
  }
  case ResourceFormat::D32_FLOAT_S8X24_UINT:
    // The depth aspect is its own standalone 4-byte float word (the first
    // of the format's two 4-byte words, `getFormatInfo`'s own comment),
    // never interleaved with stencil at all, so this is a plain copy.
    if (ToImage)
      memcpy(ImageTexel.data(), Buffer.data(), 4);
    else
      memcpy(Buffer.data(), ImageTexel.data(), 4);
    return Error::success();
  default:
    return createStringError(inconvertibleErrorCode(),
                             "depth-aspect region copy is not yet "
                             "supported for this format");
  }
}

/// The stencil-aspect peer of `copyOneDepthAspectTexel`.
Error copyOneStencilAspectTexel(ResourceFormat Format, bool ToImage,
                                MutableArrayRef<uint8_t> Buffer,
                                MutableArrayRef<uint8_t> ImageTexel) {
  switch (Format) {
  case ResourceFormat::D24_UNORM_S8_UINT: {
    uint32_t ImageWord;
    memcpy(&ImageWord, ImageTexel.data(), sizeof(ImageWord));
    if (ToImage) {
      ImageWord =
          (ImageWord & 0x00FFFFFFu) | (static_cast<uint32_t>(Buffer[0]) << 24);
      memcpy(ImageTexel.data(), &ImageWord, sizeof(ImageWord));
    } else {
      Buffer[0] = static_cast<uint8_t>(ImageWord >> 24);
    }
    return Error::success();
  }
  case ResourceFormat::D32_FLOAT_S8X24_UINT: {
    uint32_t Word1;
    memcpy(&Word1, ImageTexel.data() + 4, sizeof(Word1));
    if (ToImage) {
      Word1 = (Word1 & 0xFFFFFF00u) | static_cast<uint32_t>(Buffer[0]);
      memcpy(ImageTexel.data() + 4, &Word1, sizeof(Word1));
    } else {
      Buffer[0] = static_cast<uint8_t>(Word1 & 0xFFu);
    }
    return Error::success();
  }
  default:
    return createStringError(inconvertibleErrorCode(),
                             "stencil-aspect region copy is not yet "
                             "supported for this format");
  }
}

} // namespace

Expected<uint32_t> getDepthAspectBufferSize(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::D24_UNORM_S8_UINT:
  case ResourceFormat::D32_FLOAT_S8X24_UINT:
    return 4;
  default:
    return createStringError(inconvertibleErrorCode(),
                             "format has no separate depth-aspect buffer "
                             "encoding (only a combined depth/stencil "
                             "format's single-aspect copy needs one)");
  }
}

Expected<uint32_t> getStencilAspectBufferSize(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::D24_UNORM_S8_UINT:
  case ResourceFormat::D32_FLOAT_S8X24_UINT:
    return 1;
  default:
    return createStringError(inconvertibleErrorCode(),
                             "format has no separate stencil-aspect buffer "
                             "encoding (only a combined depth/stencil "
                             "format's single-aspect copy needs one)");
  }
}

Error copyDepthAspectRegion(ResourceFormat Format, bool ToImage,
                            MutableArrayRef<uint8_t> Buffer,
                            MutableArrayRef<uint8_t> Image,
                            uint32_t TexelCount) {
  Expected<uint32_t> BufferElemSize = getDepthAspectBufferSize(Format);
  if (!BufferElemSize)
    return BufferElemSize.takeError();
  Expected<uint32_t> ImageElemSize = getFixtureFormatElementSize(Format);
  if (!ImageElemSize)
    return ImageElemSize.takeError();
  for (uint32_t I = 0; I != TexelCount; ++I) {
    if (Error E = copyOneDepthAspectTexel(
            Format, ToImage, Buffer.slice(I * *BufferElemSize, *BufferElemSize),
            Image.slice(I * *ImageElemSize, *ImageElemSize)))
      return E;
  }
  return Error::success();
}

Error copyStencilAspectRegion(ResourceFormat Format, bool ToImage,
                              MutableArrayRef<uint8_t> Buffer,
                              MutableArrayRef<uint8_t> Image,
                              uint32_t TexelCount) {
  Expected<uint32_t> BufferElemSize = getStencilAspectBufferSize(Format);
  if (!BufferElemSize)
    return BufferElemSize.takeError();
  Expected<uint32_t> ImageElemSize = getFixtureFormatElementSize(Format);
  if (!ImageElemSize)
    return ImageElemSize.takeError();
  for (uint32_t I = 0; I != TexelCount; ++I) {
    if (Error E = copyOneStencilAspectTexel(
            Format, ToImage, Buffer.slice(I * *BufferElemSize, *BufferElemSize),
            Image.slice(I * *ImageElemSize, *ImageElemSize)))
      return E;
  }
  return Error::success();
}

namespace {

StringRef formatFixtureName(ResourceFormat Format) {
  switch (Format) {
  case ResourceFormat::Unknown:
    return "unknown";
  case ResourceFormat::R32_FLOAT:
    return "r32-float";
  case ResourceFormat::R32G32_FLOAT:
    return "r32g32-float";
  case ResourceFormat::R32G32B32_FLOAT:
    return "r32g32b32-float";
  case ResourceFormat::R32G32B32A32_FLOAT:
    return "r32g32b32a32-float";
  case ResourceFormat::R32_UINT:
    return "r32-uint";
  case ResourceFormat::R32G32_UINT:
    return "r32g32-uint";
  case ResourceFormat::R32G32B32_UINT:
    return "r32g32b32-uint";
  case ResourceFormat::R32G32B32A32_UINT:
    return "r32g32b32a32-uint";
  case ResourceFormat::R32_SINT:
    return "r32-sint";
  case ResourceFormat::R32G32_SINT:
    return "r32g32-sint";
  case ResourceFormat::R32G32B32_SINT:
    return "r32g32b32-sint";
  case ResourceFormat::R32G32B32A32_SINT:
    return "r32g32b32a32-sint";
  case ResourceFormat::R8G8B8A8_UNORM:
    return "r8g8b8a8-unorm";
  case ResourceFormat::R8G8B8A8_SNORM:
    return "r8g8b8a8-snorm";
  case ResourceFormat::R8G8B8A8_UINT:
    return "r8g8b8a8-uint";
  case ResourceFormat::R8G8B8A8_SINT:
    return "r8g8b8a8-sint";
  case ResourceFormat::R8G8B8A8_UNORM_SRGB:
    return "r8g8b8a8-unorm-srgb";
  case ResourceFormat::B8G8R8A8_UNORM:
    return "b8g8r8a8-unorm";
  case ResourceFormat::R16G16B16A16_FLOAT:
    return "r16g16b16a16-float";
  case ResourceFormat::R16G16B16A16_UNORM:
    return "r16g16b16a16-unorm";
  case ResourceFormat::R16G16B16A16_SNORM:
    return "r16g16b16a16-snorm";
  case ResourceFormat::R16G16B16A16_UINT:
    return "r16g16b16a16-uint";
  case ResourceFormat::R16G16B16A16_SINT:
    return "r16g16b16a16-sint";
  case ResourceFormat::R11G11B10_FLOAT:
    return "r11g11b10-float";
  case ResourceFormat::R10G10B10A2_UNORM:
    return "r10g10b10a2-unorm";
  case ResourceFormat::R10G10B10A2_UINT:
    return "r10g10b10a2-uint";
  case ResourceFormat::D16_UNORM:
    return "d16-unorm";
  case ResourceFormat::D32_FLOAT:
    return "d32-float";
  case ResourceFormat::D24_UNORM_S8_UINT:
    return "d24-unorm-s8-uint";
  case ResourceFormat::D32_FLOAT_S8X24_UINT:
    return "d32-float-s8x24-uint";
  case ResourceFormat::S8_UINT:
    return "s8-uint";
  case ResourceFormat::A8_UNORM:
    return "a8-unorm";
  case ResourceFormat::A1B5G5R5_UNORM:
    return "a1b5g5r5-unorm";
  // (Roadmap E19) `VK_EXT_4444_formats`: no clear-color/texel fixture
  // support exists for them yet (see `getFormatInfo` above), same as
  // E20's ASTC formats below, but they still need a name for diagnostics.
  case ResourceFormat::A4R4G4B4_UNORM:
    return "a4r4g4b4-unorm";
  case ResourceFormat::A4B4G4R4_UNORM:
    return "a4b4g4r4-unorm";
  // (Roadmap H7r) The remaining core-1.0 packed 16-bit formats.
  case ResourceFormat::R4G4B4A4_UNORM:
    return "r4g4b4a4-unorm";
  case ResourceFormat::B4G4R4A4_UNORM:
    return "b4g4r4a4-unorm";
  case ResourceFormat::R5G6B5_UNORM:
    return "r5g6b5-unorm";
  case ResourceFormat::B5G6R5_UNORM:
    return "b5g6r5-unorm";
  case ResourceFormat::R5G5B5A1_UNORM:
    return "r5g5b5a1-unorm";
  case ResourceFormat::B5G5R5A1_UNORM:
    return "b5g5r5a1-unorm";
  case ResourceFormat::A1R5G5B5_UNORM:
    return "a1r5g5b5-unorm";
  // (Roadmap H19j) `R8_{UNORM,SNORM,UINT,SINT}`: no clear-color/texel
  // fixture support exists for them yet (see `getFormatInfo` above), same
  // as the packed 16-bit formats above, but they still need a name for
  // diagnostics.
  case ResourceFormat::R8_UNORM:
    return "r8-unorm";
  case ResourceFormat::R8_SNORM:
    return "r8-snorm";
  case ResourceFormat::R8_UINT:
    return "r8-uint";
  case ResourceFormat::R8_SINT:
    return "r8-sint";
  // (Roadmap H19n) `R8G8_{UNORM,SNORM,UINT,SINT}`: same rationale as the
  // single-channel `R8` formats above.
  case ResourceFormat::R8G8_UNORM:
    return "r8g8-unorm";
  case ResourceFormat::R8G8_SNORM:
    return "r8g8-snorm";
  case ResourceFormat::R8G8_UINT:
    return "r8g8-uint";
  case ResourceFormat::R8G8_SINT:
    return "r8g8-sint";
  // (Roadmap E20) ASTC block-compressed formats: no clear-color/texel
  // fixture support exists for them yet (see `getFixtureFormatElementSize`
  // below), but they still need a name for diagnostics.
  case ResourceFormat::ASTC_4x4_UNORM:
    return "astc-4x4-unorm";
  case ResourceFormat::ASTC_4x4_SRGB:
    return "astc-4x4-srgb";
  case ResourceFormat::ASTC_5x4_UNORM:
    return "astc-5x4-unorm";
  case ResourceFormat::ASTC_5x4_SRGB:
    return "astc-5x4-srgb";
  case ResourceFormat::ASTC_5x5_UNORM:
    return "astc-5x5-unorm";
  case ResourceFormat::ASTC_5x5_SRGB:
    return "astc-5x5-srgb";
  case ResourceFormat::ASTC_6x5_UNORM:
    return "astc-6x5-unorm";
  case ResourceFormat::ASTC_6x5_SRGB:
    return "astc-6x5-srgb";
  case ResourceFormat::ASTC_6x6_UNORM:
    return "astc-6x6-unorm";
  case ResourceFormat::ASTC_6x6_SRGB:
    return "astc-6x6-srgb";
  case ResourceFormat::ASTC_8x5_UNORM:
    return "astc-8x5-unorm";
  case ResourceFormat::ASTC_8x5_SRGB:
    return "astc-8x5-srgb";
  case ResourceFormat::ASTC_8x6_UNORM:
    return "astc-8x6-unorm";
  case ResourceFormat::ASTC_8x6_SRGB:
    return "astc-8x6-srgb";
  case ResourceFormat::ASTC_8x8_UNORM:
    return "astc-8x8-unorm";
  case ResourceFormat::ASTC_8x8_SRGB:
    return "astc-8x8-srgb";
  case ResourceFormat::ASTC_10x5_UNORM:
    return "astc-10x5-unorm";
  case ResourceFormat::ASTC_10x5_SRGB:
    return "astc-10x5-srgb";
  case ResourceFormat::ASTC_10x6_UNORM:
    return "astc-10x6-unorm";
  case ResourceFormat::ASTC_10x6_SRGB:
    return "astc-10x6-srgb";
  case ResourceFormat::ASTC_10x8_UNORM:
    return "astc-10x8-unorm";
  case ResourceFormat::ASTC_10x8_SRGB:
    return "astc-10x8-srgb";
  case ResourceFormat::ASTC_10x10_UNORM:
    return "astc-10x10-unorm";
  case ResourceFormat::ASTC_10x10_SRGB:
    return "astc-10x10-srgb";
  case ResourceFormat::ASTC_12x10_UNORM:
    return "astc-12x10-unorm";
  case ResourceFormat::ASTC_12x10_SRGB:
    return "astc-12x10-srgb";
  case ResourceFormat::ASTC_12x12_UNORM:
    return "astc-12x12-unorm";
  case ResourceFormat::ASTC_12x12_SRGB:
    return "astc-12x12-srgb";
  // (Roadmap E21) HDR-only ASTC block-compressed formats: same "name only,
  // no fixture support yet" status as their LDR counterparts above.
  case ResourceFormat::ASTC_4x4_SFLOAT:
    return "astc-4x4-sfloat";
  case ResourceFormat::ASTC_5x4_SFLOAT:
    return "astc-5x4-sfloat";
  case ResourceFormat::ASTC_5x5_SFLOAT:
    return "astc-5x5-sfloat";
  case ResourceFormat::ASTC_6x5_SFLOAT:
    return "astc-6x5-sfloat";
  case ResourceFormat::ASTC_6x6_SFLOAT:
    return "astc-6x6-sfloat";
  case ResourceFormat::ASTC_8x5_SFLOAT:
    return "astc-8x5-sfloat";
  case ResourceFormat::ASTC_8x6_SFLOAT:
    return "astc-8x6-sfloat";
  case ResourceFormat::ASTC_8x8_SFLOAT:
    return "astc-8x8-sfloat";
  case ResourceFormat::ASTC_10x5_SFLOAT:
    return "astc-10x5-sfloat";
  case ResourceFormat::ASTC_10x6_SFLOAT:
    return "astc-10x6-sfloat";
  case ResourceFormat::ASTC_10x8_SFLOAT:
    return "astc-10x8-sfloat";
  case ResourceFormat::ASTC_10x10_SFLOAT:
    return "astc-10x10-sfloat";
  case ResourceFormat::ASTC_12x10_SFLOAT:
    return "astc-12x10-sfloat";
  case ResourceFormat::ASTC_12x12_SFLOAT:
    return "astc-12x12-sfloat";
  }
  llvm_unreachable("unhandled ResourceFormat");
}

/// Reads one texel's worth of bytes (`Info.Components * Info.ComponentBytes`)
/// out of \p Token into \p Out, in the encoding `getFormatInfo` selects for
/// \p Info. Returns an `Error` for a malformed token.
Error decodeTexel(StringRef Token, const FormatInfo &Info,
                  MutableArrayRef<uint8_t> Out) {
  if (Info.IsFloat) {
    SmallVector<StringRef, 4> Components;
    Token.split(Components, ',');
    if (Components.size() != Info.Components)
      return createStringError(inconvertibleErrorCode(),
                               "texel '%s' has %zu component(s), expected %u",
                               Token.str().c_str(), Components.size(),
                               Info.Components);
    for (unsigned I = 0; I != Info.Components; ++I) {
      double Value;
      if (Components[I].getAsDouble(Value))
        return createStringError(inconvertibleErrorCode(),
                                 "'%s' is not a valid floating-point texel "
                                 "component",
                                 Components[I].str().c_str());
      float F = static_cast<float>(Value);
      memcpy(Out.data() + I * Info.ComponentBytes, &F, Info.ComponentBytes);
    }
    return Error::success();
  }

  if (Token.size() != Info.Components * Info.ComponentBytes * 2)
    return createStringError(inconvertibleErrorCode(),
                             "texel '%s' has the wrong number of hex digits "
                             "for its format",
                             Token.str().c_str());
  for (unsigned I = 0; I != Info.Components; ++I) {
    StringRef Chunk =
        Token.substr(I * Info.ComponentBytes * 2, Info.ComponentBytes * 2);
    uint64_t Value;
    if (Chunk.getAsInteger(16, Value))
      return createStringError(inconvertibleErrorCode(),
                               "'%s' is not a valid hexadecimal texel "
                               "component",
                               Chunk.str().c_str());
    memcpy(Out.data() + I * Info.ComponentBytes, &Value, Info.ComponentBytes);
  }
  return Error::success();
}

/// The inverse of `decodeTexel`: prints one texel's raw bytes as a single
/// whitespace-delimited token.
void encodeTexel(raw_ostream &OS, const FormatInfo &Info,
                 ArrayRef<uint8_t> Texel) {
  for (unsigned I = 0; I != Info.Components; ++I) {
    if (I != 0 && Info.IsFloat)
      OS << ',';
    if (Info.IsFloat) {
      float F;
      memcpy(&F, Texel.data() + I * Info.ComponentBytes, Info.ComponentBytes);
      OS << format("%+.4e", static_cast<double>(F));
      continue;
    }
    uint64_t Value = 0;
    memcpy(&Value, Texel.data() + I * Info.ComponentBytes, Info.ComponentBytes);
    OS << format_hex_no_prefix(Value, Info.ComponentBytes * 2);
  }
}

} // namespace

Expected<std::vector<ImageFixture>> parseImageFixtures(StringRef Text) {
  std::vector<ImageFixture> Result;

  SmallVector<StringRef, 32> Lines;
  Text.split(Lines, '\n');

  auto StripComment = [](StringRef Line) {
    size_t Hash = Line.find('#');
    return (Hash == StringRef::npos ? Line : Line.substr(0, Hash)).trim();
  };

  size_t I = 0;
  while (I < Lines.size()) {
    StringRef Line = StripComment(Lines[I++]);
    if (Line.empty())
      continue;

    if (!Line.consume_front("image "))
      return createStringError(inconvertibleErrorCode(),
                               "expected an 'image' header line, got '%s'",
                               Line.str().c_str());

    SmallVector<StringRef, 8> Fields;
    Line.split(Fields, ' ', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    if (Fields.size() < 3)
      return createStringError(
          inconvertibleErrorCode(),
          "an 'image' header needs a name, extent and format");

    ImageFixture Img;
    Img.Name = Fields[0].str();

    SmallVector<StringRef, 3> Extent;
    Fields[1].split(Extent, 'x');
    if (Extent.size() < 2 || Extent.size() > 3 ||
        Extent[0].getAsInteger(10, Img.Width) ||
        Extent[1].getAsInteger(10, Img.Height))
      return createStringError(inconvertibleErrorCode(),
                               "'%s' is not a valid image extent",
                               Fields[1].str().c_str());
    if (Extent.size() == 3 && Extent[2].getAsInteger(10, Img.Depth))
      return createStringError(inconvertibleErrorCode(),
                               "'%s' is not a valid image extent",
                               Fields[1].str().c_str());

    Expected<ResourceFormat> Format = parseFixtureFormat(Fields[2]);
    if (!Format)
      return Format.takeError();
    Img.Format = *Format;

    for (StringRef Field : ArrayRef(Fields).drop_front(3)) {
      if (Field.consume_front("mip=")) {
        if (Field.getAsInteger(10, Img.Mip))
          return createStringError(inconvertibleErrorCode(),
                                   "'mip=%s' is not a valid mip level",
                                   Field.str().c_str());
      } else if (Field.consume_front("slice=")) {
        if (Field.getAsInteger(10, Img.Slice))
          return createStringError(inconvertibleErrorCode(),
                                   "'slice=%s' is not a valid array slice",
                                   Field.str().c_str());
      } else
        return createStringError(inconvertibleErrorCode(),
                                 "unknown image header field '%s'",
                                 Field.str().c_str());
    }

    Expected<FormatInfo> Info = getFormatInfo(Img.Format);
    if (!Info)
      return Info.takeError();
    uint32_t ElemSize = Info->Components * Info->ComponentBytes;
    Img.Data.assign((size_t)Img.Width * Img.Height * ElemSize, 0);

    for (uint32_t Row = 0; Row != Img.Height; ++Row) {
      if (I >= Lines.size())
        return createStringError(inconvertibleErrorCode(),
                                 "image '%s' is missing row y=%u",
                                 Img.Name.c_str(), Row);
      StringRef RowLine = StripComment(Lines[I++]);
      std::string PrefixStr = ("y=" + Twine(Row) + ":").str();
      if (!RowLine.consume_front(PrefixStr))
        return createStringError(inconvertibleErrorCode(),
                                 "expected 'y=%u:' row, got '%s'", Row,
                                 RowLine.str().c_str());

      SmallVector<StringRef, 16> Tokens;
      RowLine.split(Tokens, ' ', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
      if (Tokens.size() != Img.Width)
        return createStringError(inconvertibleErrorCode(),
                                 "row y=%u of image '%s' has %zu texel(s), "
                                 "expected %u",
                                 Row, Img.Name.c_str(), Tokens.size(),
                                 Img.Width);

      for (uint32_t Col = 0; Col != Img.Width; ++Col) {
        uint8_t *Dest = Img.Data.data() + (Row * Img.Width + Col) * ElemSize;
        if (Error E = decodeTexel(Tokens[Col], *Info,
                                  MutableArrayRef(Dest, ElemSize)))
          return std::move(E);
      }
    }

    Result.push_back(std::move(Img));
  }

  return Result;
}

Error printImageFixture(raw_ostream &OS, const ImageFixture &Image) {
  Expected<FormatInfo> Info = getFormatInfo(Image.Format);
  if (!Info)
    return Info.takeError();
  uint32_t ElemSize = Info->Components * Info->ComponentBytes;
  if (Image.Data.size() != (size_t)Image.Width * Image.Height * ElemSize)
    return createStringError(inconvertibleErrorCode(),
                             "image '%s' data size does not match its "
                             "width/height/format",
                             Image.Name.c_str());

  OS << "image " << Image.Name << ' ' << Image.Width << 'x' << Image.Height;
  if (Image.Depth != 1)
    OS << 'x' << Image.Depth;
  OS << ' ' << formatFixtureName(Image.Format);
  if (Image.Mip != 0)
    OS << " mip=" << Image.Mip;
  if (Image.Slice != 0)
    OS << " slice=" << Image.Slice;
  OS << '\n';

  for (uint32_t Row = 0; Row != Image.Height; ++Row) {
    OS << "  y=" << Row << ':';
    for (uint32_t Col = 0; Col != Image.Width; ++Col) {
      OS << ' ';
      const uint8_t *Texel =
          Image.Data.data() + (Row * Image.Width + Col) * ElemSize;
      encodeTexel(OS, *Info, ArrayRef(Texel, ElemSize));
    }
    OS << '\n';
  }
  return Error::success();
}

} // namespace feme::graphics
