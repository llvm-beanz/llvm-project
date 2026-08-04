//===- SignatureComments.h - fxc signature table reader -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares parseSignatureComments, which recovers a shader's input, output
// and patch-constant signatures from the tables `fxc` prints above its
// disassembly. Those tables carry the whole of the legacy `ISGN`/`OSGN`/
// `PCSG` parts -- element name, semantic index, register, write mask,
// system value and component type -- which the instruction stream alone
// does not: a `dcl_input_ps v0.yz` says nothing about the element's name,
// its component type, or the components of the register some other stage
// wrote but this one does not read.
//
// See feme/docs/Design.md's "dxbc-as" section.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_SIGNATURECOMMENTS_H
#define FEME_DXBC_ASSEMBLER_SIGNATURECOMMENTS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace feme {
namespace dxbc {

/// One row of an `fxc` signature table, in the terms the legacy
/// `dxbc::LegacySignatureElement` on-disk layout uses.
struct SignatureElement {
  std::string Name;
  uint32_t Index = 0;
  llvm::dxbc::D3DSystemValue SystemValue =
      llvm::dxbc::D3DSystemValue::Undefined;
  llvm::dxbc::SigComponentType CompType = llvm::dxbc::SigComponentType::Float32;
  /// The register the element occupies, or `NoRegister` for one that names
  /// no register at all (`oDepth` and friends).
  uint32_t Register = 0;
  uint8_t Mask = 0;
  uint8_t ExclusiveMask = 0;

  static constexpr uint32_t NoRegister = ~0u;
};

/// A shader's three signatures. \c Seen distinguishes a table that was
/// present but empty (`fxc` prints "no Input") from one that was not
/// printed at all, because the former should still produce an -- empty --
/// container part.
struct Signatures {
  std::vector<SignatureElement> Input;
  std::vector<SignatureElement> Output;
  std::vector<SignatureElement> PatchConstant;
  bool SeenInput = false;
  bool SeenOutput = false;
  bool SeenPatchConstant = false;

  bool empty() const { return !SeenInput && !SeenOutput && !SeenPatchConstant; }
};

/// Reads the signature tables out of \p Source's comments. Malformed or
/// unrecognized rows are skipped rather than diagnosed: the tables are
/// documentation `fxc` emits alongside the assembly, so a fixture that
/// omits or truncates them is still assemblable.
Signatures parseSignatureComments(llvm::StringRef Source);

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_SIGNATURECOMMENTS_H
