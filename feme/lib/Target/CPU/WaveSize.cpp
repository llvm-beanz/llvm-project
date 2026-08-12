//===- WaveSize.cpp - FeMe CPU target wave size resolution ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/WaveSize.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace feme::cpu {

std::optional<ShaderWaveSizeRequirement>
parseShaderWaveSizeAttr(StringRef Attr) {
  SmallVector<StringRef, 3> Components;
  Attr.split(Components, ',');
  if (Components.size() != 3)
    return std::nullopt;

  unsigned Values[3];
  for (unsigned I = 0; I != 3; ++I)
    if (Components[I].getAsInteger(10, Values[I]))
      return std::nullopt;

  ShaderWaveSizeRequirement Req;
  Req.Min = Values[0];
  Req.Max = Values[1];
  Req.Preferred = Values[2];
  return Req;
}

namespace {

/// Returns an `Error` if \p W is not a legal wave size (a power of two in
/// `[MinWaveSize, MaxWaveSize]`), naming \p Source ("--wave-size" or the
/// shader's declared requirement) in the diagnostic -- required "wherever it
/// comes from", per "Wave Size Selection" in feme/docs/FeMeCPUDesign.md.
Error checkLegalWaveSize(unsigned W, StringRef Source) {
  if (W < MinWaveSize || W > MaxWaveSize || !isPowerOf2_32(W))
    return createStringError(
        inconvertibleErrorCode(),
        "%s wave size %u is not a power of two in [%u, %u]",
        Source.str().c_str(), W, MinWaveSize, MaxWaveSize);
  return Error::success();
}

/// The host-derived default absent any user or shader opinion: `max(4,
/// HostVectorBits / 32)`, rounded down to a power of two and clamped to
/// `MaxWaveSize`. See "Wave Size Selection" in feme/docs/FeMeCPUDesign.md
/// for the rationale.
unsigned hostDefaultWaveSize(unsigned HostVectorBits) {
  unsigned W = std::max(MinWaveSize, HostVectorBits / 32);
  W = bit_floor(W);
  return std::min(W, MaxWaveSize);
}

} // namespace

Expected<unsigned>
resolveWaveSize(std::optional<unsigned> UserWaveSize,
                std::optional<ShaderWaveSizeRequirement> ShaderRequirement,
                unsigned HostVectorBits) {
  if (UserWaveSize)
    if (Error E = checkLegalWaveSize(*UserWaveSize, "--wave-size"))
      return std::move(E);

  if (ShaderRequirement) {
    if (Error E =
            checkLegalWaveSize(ShaderRequirement->Min, "the shader's required"))
      return std::move(E);
    if (ShaderRequirement->Max)
      if (Error E = checkLegalWaveSize(ShaderRequirement->Max,
                                       "the shader's required"))
        return std::move(E);
    if (ShaderRequirement->Preferred)
      if (Error E = checkLegalWaveSize(ShaderRequirement->Preferred,
                                       "the shader's required"))
        return std::move(E);
  }

  // Neither party has an opinion: the host-derived default.
  if (!UserWaveSize && !ShaderRequirement)
    return hostDefaultWaveSize(HostVectorBits);

  // Only the shader has an opinion: its preferred size, else the low end of
  // its accepted range.
  if (!UserWaveSize)
    return ShaderRequirement->preferredSize();

  // Only the user has an opinion.
  if (!ShaderRequirement)
    return *UserWaveSize;

  // Both have an opinion: they must agree, i.e. the user's value must be one
  // the shader accepts. A shader declaring a required wave size is
  // asserting that its algorithm depends on that size, so a mismatch is a
  // hard error rather than a silent override -- see "Wave Size Selection".
  if (!ShaderRequirement->accepts(*UserWaveSize))
    return createStringError(
        inconvertibleErrorCode(),
        "requested wave size %u conflicts with the shader's required "
        "wave size %s",
        *UserWaveSize,
        ShaderRequirement->Max
            ? ("[" + std::to_string(ShaderRequirement->Min) + ", " +
               std::to_string(ShaderRequirement->Max) + "]")
                  .c_str()
            : std::to_string(ShaderRequirement->Min).c_str());
  return *UserWaveSize;
}

} // namespace feme::cpu
