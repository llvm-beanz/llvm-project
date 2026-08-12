//===- WaveSize.h - FeMe CPU target wave size resolution --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the wave size (`W`) resolution logic described in the
// "Wave Size Selection" section of feme/docs/FeMeCPUDesign.md: `W` must be a
// power of two in [4, 128], and up to two independent parties -- the user
// (`--wave-size`/`-feme-wave-size`/`JITOptions::WaveSize`) and the shader
// (its `"hlsl.wavesize"` function attribute, see
// feme::dxil::MetadataRaisingPass) -- may express an opinion about it. This
// header is deliberately free of any command-line or module-attribute
// parsing of its own: `parseShaderWaveSizeAttr` turns the raised
// `"hlsl.wavesize"` string into a `ShaderWaveSizeRequirement`, and
// `resolveWaveSize` applies the resolution table to whatever the caller
// already extracted, so it is equally usable from `feme`'s `DriverOptions`,
// `feme-opt`'s `-feme-wave-size`, and `feme::cpu::JITOptions`.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TARGET_CPU_WAVESIZE_H
#define FEME_TARGET_CPU_WAVESIZE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <optional>

namespace feme::cpu {

/// The lower/upper bound `W` must fall within, per "Wave Size Selection" in
/// feme/docs/FeMeCPUDesign.md. There is no scalar (`W = 1`) configuration:
/// see that section for why.
constexpr unsigned MinWaveSize = 4;
constexpr unsigned MaxWaveSize = 128;

/// A shader's required wave size, parsed from its `"hlsl.wavesize"` function
/// attribute (`"min,max,preferred"`, see feme::dxil::MetadataRaisingPass).
/// `Max == 0` means the shader requires exactly `Min` (SM 6.6's
/// single-value form, widened to `"n,0,0"`); a nonzero `Max` means the
/// shader accepts any size in `[Min, Max]`, preferring `Preferred` if
/// nonzero, else `Min`.
struct ShaderWaveSizeRequirement {
  unsigned Min = 0;
  unsigned Max = 0;
  unsigned Preferred = 0;

  /// The single size this requirement resolves to absent a user override:
  /// `Preferred` if given, else the low end of the accepted range.
  unsigned preferredSize() const { return Preferred ? Preferred : Min; }

  /// Whether \p W is one of the sizes this requirement accepts.
  bool accepts(unsigned W) const {
    return Max ? (W >= Min && W <= Max) : W == Min;
  }
};

/// Parses a raised `"hlsl.wavesize"` function attribute value (see
/// feme::dxil::MetadataRaisingPass) into a `ShaderWaveSizeRequirement`.
/// Returns `std::nullopt` if \p Attr is malformed (not exactly three
/// comma-separated non-negative integers) -- callers should treat that the
/// same as the shader declaring no requirement at all, since
/// `MetadataRaisingPass` never emits a malformed attribute itself.
std::optional<ShaderWaveSizeRequirement>
parseShaderWaveSizeAttr(llvm::StringRef Attr);

/// Applies the "Wave Size Selection" resolution table: given an optional
/// user-requested wave size, an optional shader requirement, and the host's
/// vector width (used only for the "neither party has an opinion" default,
/// see that section), returns the resolved `W`, or a diagnostic `Error` if
/// the request is invalid (out of `[MinWaveSize, MaxWaveSize]`, not a power
/// of two, or a user/shader conflict).
llvm::Expected<unsigned>
resolveWaveSize(std::optional<unsigned> UserWaveSize,
                std::optional<ShaderWaveSizeRequirement> ShaderRequirement,
                unsigned HostVectorBits);

} // namespace feme::cpu

#endif // FEME_TARGET_CPU_WAVESIZE_H
