//===- ImageFixture.h - Textual image fixture read/write ---------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares feme::graphics::parseImageFixtures/printImageFixture,
// the reader/writer for the textual image fixture format specified in
// "Textual scene and image fixtures" in feme/docs/Design.md (roadmap R31,
// "FeMeGraphics skeleton" -- see feme/docs/Roadmap.md). One format serves as
// both an input texture and an expected or actual-output attachment dump,
// so `unittests/Graphics/` and `feme-render` (docs/CommandGuide/
// feme-render.md) share exactly this reader/writer rather than each
// growing its own.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_GRAPHICS_IMAGEFIXTURE_H
#define FEME_GRAPHICS_IMAGEFIXTURE_H

#include "feme/Target/CPU/RuntimeABI.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
class raw_ostream;
} // namespace llvm

namespace feme::graphics {

/// One parsed/printed image fixture block: a single 2D/3D image
/// subresource's name, extent, format, optional mip level/array slice
/// (each mip/array slice of a texture is a separate block, per the file
/// comment above), and tightly packed row-major texel data in the format's
/// own storage encoding -- never a converted one, so a fixture never
/// silently depends on the conversion it is testing.
struct ImageFixture {
  std::string Name;
  cpu::ResourceFormat Format = cpu::ResourceFormat::Unknown;
  uint32_t Width = 0;
  uint32_t Height = 0;
  uint32_t Depth = 1;
  uint32_t Mip = 0;
  uint32_t Slice = 0;
  std::vector<uint8_t> Data;
};

/// Parses every `image <name> <extent> <format> [mip=<n>] [slice=<n>]`
/// block \p Text contains, in the textual encoding "Textual scene and image
/// fixtures" in feme/docs/Design.md specifies. Returns an `Error` for a
/// malformed header, an unrecognized or not-yet-supported format (see
/// `parseFixtureFormat`'s own comment), a row/column count that disagrees
/// with the header's extent, or a malformed texel token.
llvm::Expected<std::vector<ImageFixture>>
parseImageFixtures(llvm::StringRef Text);

/// Prints \p Image in the same textual encoding `parseImageFixtures`
/// accepts, so an actual-output dump can be pasted into a `CHECK` line (see
/// the file comment above).
llvm::Error printImageFixture(llvm::raw_ostream &OS, const ImageFixture &Image);

/// Parses a fixture/scene `format` field -- the hyphen-separated spelling
/// (`r8g8b8a8-unorm`) feme/docs/Design.md's own examples use, for both an
/// image fixture and a scene attachment's format (see "Textual scene and
/// image fixtures": "the same format table"). Exposed for `feme-render`'s
/// scene parser, which needs to build attachment storage in the same
/// format space image fixtures use.
llvm::Expected<cpu::ResourceFormat> parseFixtureFormat(llvm::StringRef Format);

/// The byte size of one texel of \p Format in the fixture/scene format
/// table, or an `Error` for a format `getFormatInfo` (ImageFixture.cpp's
/// internal table) does not implement yet.
llvm::Expected<uint32_t>
getFixtureFormatElementSize(cpu::ResourceFormat Format);

/// Whether \p Format's components are IEEE-754 floats (true) or
/// integer/normalized (false) in the fixture/scene format table. Used by
/// `feme-render`'s `--tolerance` comparison, which is only meaningful for
/// a floating-point format.
llvm::Expected<bool> isFixtureFormatFloat(cpu::ResourceFormat Format);

/// Packs one texel's worth of clear-color components (`attachments[].clear`
/// in the scene YAML, see feme/docs/Design.md's "Textual scene and image
/// fixtures") into \p Texel, in \p Format's storage encoding. A
/// floating-point format stores each component as-is; `R8G8B8A8_UNORM`/
/// `_UNORM_SRGB` treat each component as a `[0, 1]` value scaled to a byte
/// (sRGB encode-on-store is a later milestone, G4 -- see "Texture layout
/// and formats" in feme/docs/FeMeGraphicsDesign.md's Status note). Any
/// other format is an `Error`: a mechanical, on-demand addition once a
/// test needs it, the same as `getFormatInfo`'s own scope note.
llvm::Error packClearColor(cpu::ResourceFormat Format,
                           llvm::ArrayRef<double> Clear,
                           llvm::MutableArrayRef<uint8_t> Texel);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_IMAGEFIXTURE_H
