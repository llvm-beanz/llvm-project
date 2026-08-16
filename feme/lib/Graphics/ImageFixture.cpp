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
          .Case("r16g16b16a16-float", ResourceFormat::R16G16B16A16_FLOAT)
          .Case("r16g16b16a16-unorm", ResourceFormat::R16G16B16A16_UNORM)
          .Case("r16g16b16a16-snorm", ResourceFormat::R16G16B16A16_SNORM)
          .Case("r16g16b16a16-uint", ResourceFormat::R16G16B16A16_UINT)
          .Case("r16g16b16a16-sint", ResourceFormat::R16G16B16A16_SINT)
          .Case("r11g11b10-float", ResourceFormat::R11G11B10_FLOAT)
          .Case("r10g10b10a2-unorm", ResourceFormat::R10G10B10A2_UNORM)
          .Case("r10g10b10a2-uint", ResourceFormat::R10G10B10A2_UINT)
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

  if (Format != ResourceFormat::R8G8B8A8_UNORM &&
      Format != ResourceFormat::R8G8B8A8_UNORM_SRGB)
    return createStringError(inconvertibleErrorCode(),
                             "attachment clear color is not yet supported "
                             "for this format");
  for (unsigned I = 0; I != Info->Components; ++I) {
    double Clamped = std::clamp(Clear[I], 0.0, 1.0);
    Texel[I] = static_cast<uint8_t>(std::lround(Clamped * 255.0));
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
