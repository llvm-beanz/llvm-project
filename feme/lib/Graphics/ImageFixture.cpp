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
  default:
    return createStringError(inconvertibleErrorCode(),
                             "stencil unpack is not yet supported for this "
                             "format");
  }
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
