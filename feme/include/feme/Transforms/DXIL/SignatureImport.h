//===- SignatureImport.h - DXIL !dx.entryPoints signature import -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DXIL-specific half of roadmap R18: converting one
// `!dx.entryPoints` entry's input/output/patch-constant signature rows into
// feme's source-independent `feme::EntrySignature` model
// (feme/include/feme/Core/Signature.h), plus preserving an entry's raw
// root-signature bytes (the `EntryRootSigTag` entry property). Both are
// recorded as function metadata so they survive
// `feme::dxil::MetadataRaisingPass` erasing `!dx.entryPoints` itself -- see
// "Signature reflection" in feme/docs/FeMeGraphicsDesign.md.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_TRANSFORMS_DXIL_SIGNATUREIMPORT_H
#define FEME_TRANSFORMS_DXIL_SIGNATUREIMPORT_H

#include "feme/Core/ShaderStage.h"
#include "feme/Core/Signature.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace llvm {
class Function;
class MDNode;
} // namespace llvm

namespace feme::dxil {

/// Converts \p Signatures -- a `!dx.entryPoints` entry's `Signatures` tuple
/// operand (`{InputSignature, OutputSignature, PatchConstantSignature}`, any
/// of which may be null) -- into a `feme::EntrySignature`. Returns an empty
/// signature if \p Signatures itself is null (an entry with no signature at
/// all, e.g. a DXIL library's non-shader function).
///
/// \p Stage decides the patch-constant list's direction: a domain shader
/// consumes patch-constant rows (`PatchInput`), while every other stage that
/// carries one (in practice, only a hull shader) produces them
/// (`PatchOutput`).
feme::EntrySignature convertEntrySignature(const llvm::MDNode *Signatures,
                                            feme::ShaderStage Stage);

/// The name of the function metadata node `setEntrySignature`/
/// `getEntrySignature` attach a raised entry point's serialized
/// `feme::EntrySignature` under.
llvm::StringRef getEntrySignatureMDKind();

/// Records \p Sig as \p F's `getEntrySignatureMDKind()` function metadata,
/// as `feme::serializeSignature(Sig)`'s bytes.
void setEntrySignature(llvm::Function &F, const feme::EntrySignature &Sig);

/// Reads back the `feme::EntrySignature` `setEntrySignature` attached to
/// \p F, or `std::nullopt` if \p F carries none (or it fails to parse as one
/// -- see `feme::parseSignature`).
std::optional<feme::EntrySignature> getEntrySignature(const llvm::Function &F);

/// The name of the function metadata node `setRootSignature`/
/// `getRootSignature` attach an entry point's raw root-signature bytes
/// under.
llvm::StringRef getRootSignatureMDKind();

/// Records \p Bytes -- an entry point's serialized root signature, taken
/// verbatim from its `EntryRootSigTag` (12) property -- as \p F's
/// `getRootSignatureMDKind()` function metadata. FeMe does not parse the
/// root-signature blob itself yet (that is roadmap W2); this only prevents
/// it from being lost when `!dx.entryPoints` is erased.
void setRootSignature(llvm::Function &F, llvm::ArrayRef<uint8_t> Bytes);

/// Reads back the root-signature bytes `setRootSignature` attached to \p F,
/// or `std::nullopt` if \p F carries none.
std::optional<std::vector<uint8_t>> getRootSignature(const llvm::Function &F);

} // namespace feme::dxil

#endif // FEME_TRANSFORMS_DXIL_SIGNATUREIMPORT_H
