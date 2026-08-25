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
/// and formats" in feme/docs/FeMeGraphicsDesign.md's Status note).
/// `B8G8R8A8_UNORM` is the same encoding with red and blue swapped in
/// storage, and `R10G10B10A2_UNORM` packs all four components into one
/// 32-bit word (roadmap C1). Any other format is an `Error`: a mechanical,
/// on-demand addition once a test needs it, the same as `getFormatInfo`'s
/// own scope note.
llvm::Error packClearColor(cpu::ResourceFormat Format,
                           llvm::ArrayRef<double> Clear,
                           llvm::MutableArrayRef<uint8_t> Texel);

/// The inverse of `packClearColor`: unpacks one texel's worth of \p Format
/// components from \p Texel into \p Out as `[0, 1]`-range (or raw, for a
/// floating-point format) doubles. Used by blending (roadmap R33), which
/// needs an attachment's *existing* color as an operand alongside a
/// fragment's new one. Supports the same format subset `packClearColor`
/// does, plus every floating-point format `getFormatInfo` already
/// describes generically.
llvm::Error unpackColor(cpu::ResourceFormat Format,
                        llvm::ArrayRef<uint8_t> Texel,
                        llvm::MutableArrayRef<double> Out);

/// Packs \p Depth (a `[0, 1]` fraction, matching
/// `VkClearDepthStencilValue::depth`) into \p Texel's depth component, in
/// \p Format's storage encoding. For a pure depth format (`D16_UNORM`/
/// `D32_FLOAT`) this writes the whole texel; for the combined
/// `D24_UNORM_S8_UINT` format (roadmap C1) it is a read-modify-write that
/// only touches the low 24 bits, leaving whatever stencil value already
/// occupies the high byte untouched -- the two attachment "halves" of a
/// combined format share the same storage, so packing one must never
/// clobber the other.
llvm::Error packDepthClear(cpu::ResourceFormat Format, double Depth,
                           llvm::MutableArrayRef<uint8_t> Texel);

/// The inverse of `packDepthClear`: unpacks \p Texel's depth component as a
/// `[0, 1]` fraction.
llvm::Error unpackDepth(cpu::ResourceFormat Format,
                        llvm::ArrayRef<uint8_t> Texel, double &Depth);

/// Packs \p Stencil (an integer reference value, matching
/// `VkClearDepthStencilValue::stencil`) into \p Texel's stencil component,
/// in \p Format's storage encoding. For `S8_UINT` this writes the whole
/// (one-byte) texel; for the combined `D24_UNORM_S8_UINT` format it is a
/// read-modify-write of only the high byte, preserving the low 24 bits'
/// depth value -- see `packDepthClear`'s comment.
llvm::Error packStencilClear(cpu::ResourceFormat Format, uint32_t Stencil,
                             llvm::MutableArrayRef<uint8_t> Texel);

/// The inverse of `packStencilClear`: unpacks \p Texel's stencil component
/// as a raw integer.
llvm::Error unpackStencil(cpu::ResourceFormat Format,
                          llvm::ArrayRef<uint8_t> Texel, uint32_t &Stencil);

/// The buffer-side byte size of one texel of \p Format's depth aspect
/// alone, the layout a `VkBufferImageCopy`/`VkMemoryToImageCopy`/
/// `VkImageToMemoryCopy` naming `VK_IMAGE_ASPECT_DEPTH_BIT` uses (Vulkan
/// spec "Buffer and Image Addressing"): 4 bytes for both
/// `D24_UNORM_S8_UINT` (a 32-bit word, the D24 value in the low 24 bits,
/// the upper 8 undefined) and `D32_FLOAT_S8X24_UINT` (an IEEE float) --
/// never the combined format's own, larger `getFixtureFormatElementSize`,
/// which describes a copy of *both* aspects together. An `Error` for any
/// other format: a single-aspect depth copy is only meaningful for a
/// combined depth/stencil format (roadmap F11a).
llvm::Expected<uint32_t> getDepthAspectBufferSize(cpu::ResourceFormat Format);

/// The stencil-aspect peer of `getDepthAspectBufferSize`: always 1 byte
/// (`S8_UINT`'s own tightly packed size) for either combined format.
llvm::Expected<uint32_t> getStencilAspectBufferSize(cpu::ResourceFormat Format);

/// Copies \p TexelCount texels' depth aspect between \p Buffer (tightly
/// packed, one `getDepthAspectBufferSize`-sized element per texel) and
/// \p Image (tightly packed, one full interleaved combined depth/stencil
/// texel per texel, in \p Format's storage encoding), in whichever
/// direction \p ToImage selects. A buffer-to-image copy is a per-texel
/// read-modify-write that leaves each image texel's existing stencil bits
/// untouched -- the two aspects share one texel's worth of storage, see
/// `packDepthClear`'s own comment -- while an image-to-buffer copy simply
/// reads the depth bits back out. The per-region generalization of
/// `packDepthClear`/`unpackDepth`'s single repeated clear value to an
/// arbitrary buffer region (roadmap F11a), used by `copyBufferImageRegion`
/// (ImageOps.cpp) for a single-aspect `VkBufferImageCopy`/
/// `VkMemoryToImageCopy`/`VkImageToMemoryCopy` of a combined depth/stencil
/// image.
llvm::Error copyDepthAspectRegion(cpu::ResourceFormat Format, bool ToImage,
                                  llvm::MutableArrayRef<uint8_t> Buffer,
                                  llvm::MutableArrayRef<uint8_t> Image,
                                  uint32_t TexelCount);

/// The stencil-aspect peer of `copyDepthAspectRegion`.
llvm::Error copyStencilAspectRegion(cpu::ResourceFormat Format, bool ToImage,
                                    llvm::MutableArrayRef<uint8_t> Buffer,
                                    llvm::MutableArrayRef<uint8_t> Image,
                                    uint32_t TexelCount);

} // namespace feme::graphics

#endif // FEME_GRAPHICS_IMAGEFIXTURE_H
