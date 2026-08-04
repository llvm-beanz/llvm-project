//===- Encoder.h - DXBC tokenized bytecode encoder ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares Encoder, which lowers a parsed Program to raw DXBC tokenized
// shader bytecode (and, optionally, a full DXContainer wrapping it). This is
// the third and final stage of dxbc-as's lex -> parse -> encode pipeline
// (see feme/docs/Design.md's "dxbc-as" section).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_ENCODER_H
#define FEME_DXBC_ASSEMBLER_ENCODER_H

#include "feme/DXBC/Assembler/Parser.h"
#include "feme/DXBC/Assembler/SignatureComments.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"

#include <cstdint>

namespace feme {
namespace dxbc {

/// Encodes \p Program as a raw DXBC tokenized shader bytecode blob: an
/// optional version/length token pair, then each instruction's
/// opcode/operand tokens back to back, following Microsoft's public
/// `d3d11TokenizedProgramFormat.hpp` token layout (see Encoder.cpp for the
/// specific bit layouts implemented).
///
/// Fails (returns an llvm::Error) if \p Program contains something this
/// Encoder cannot represent, e.g. an instruction longer than the 127-DWORD
/// limit the length field can express.
llvm::Expected<llvm::SmallVector<uint32_t, 64>>
encodeProgram(const Program &Program);

/// Wraps \p Bytecode (as produced by encodeProgram) in a minimal DXContainer
/// (see llvm/include/llvm/BinaryFormat/DXContainer.h) holding a "SHEX"
/// part, preceded by the legacy "ISGN"/"OSGN"/"PCSG" signature parts
/// \p Sig describes, and appends the resulting bytes to \p Out. A
/// signature `fxc` did not print at all contributes no part; one it
/// printed as empty contributes an empty part, which is what `fxc` itself
/// emits.
///
/// Deviation: real DXContainers carry a checksum (\c Header::FileHash)
/// computed with a bespoke, undocumented-by-Microsoft hash over the file
/// contents; this encoder leaves it zeroed. No in-tree consumer of
/// DXContainer (llvm::object::DXContainer, the `dxsa` importer) validates
/// that hash, so this is a safe simplification for a testing tool -- see
/// feme/docs/Design.md's "dxbc-as" section.
void wrapInContainer(llvm::ArrayRef<uint32_t> Bytecode, const Signatures &Sig,
                     llvm::SmallVectorImpl<char> &Out);

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_ENCODER_H
