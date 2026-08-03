//===- Encoder.h - DXBC tokenized bytecode encoder ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares Encoder, which lowers a parsed instruction stack to raw DXBC
// tokenized shader bytecode (and, optionally, a full DXContainer wrapping
// it). This is the third and final stage of dxbc-as's lex -> parse -> encode
// pipeline (see feme/docs/Design.md's "dxbc-as" section).
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_ENCODER_H
#define FEME_DXBC_ASSEMBLER_ENCODER_H

#include "feme/DXBC/Assembler/Instruction.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"

#include <cstdint>
#include <vector>

namespace feme {
namespace dxbc {

/// Which real D3D10_SB_TOKENIZED_PROGRAM_TYPE to declare in the program's
/// version token (see encodeProgram). Only the shader stages relevant to
/// the mnemonics dxbc-as currently supports are exposed.
enum class ShaderKind : uint16_t {
  Pixel = 0,
  Vertex = 1,
  Compute = 5,
};

/// Encodes \p Program as a raw DXBC tokenized shader bytecode blob: a
/// version token, a length token, then each instruction's opcode/operand
/// tokens back to back, following Microsoft's public
/// `d3d11TokenizedProgramFormat.hpp` token layout (see Opcodes.def and
/// Encoder.cpp for the specific bit layouts implemented).
///
/// Fails (returns an llvm::Error) if \p Program contains an operand
/// combination this Encoder cannot represent, e.g. a `_sat` suffix on a
/// non-floating-point opcode; this can only happen by hand-constructing an
/// invalid Instruction; parseAssembly never produces one.
llvm::Expected<llvm::SmallVector<uint32_t, 64>>
encodeProgram(llvm::ArrayRef<Instruction> Program, ShaderKind Kind);

/// Wraps \p Bytecode (as produced by encodeProgram) in a minimal DXContainer
/// (see llvm/include/llvm/BinaryFormat/DXContainer.h) holding a single
/// "SHEX" part, and appends the resulting bytes to \p Out.
///
/// Deviation: real DXContainers carry a checksum (\c Header::FileHash)
/// computed with a bespoke, undocumented-by-Microsoft hash over the file
/// contents; this encoder leaves it zeroed. No in-tree consumer of
/// DXContainer (llvm::object::DXContainer, this tool's future importer
/// counterpart) validates that hash, so this is a safe simplification for a
/// testing tool -- see feme/docs/Design.md's "dxbc-as" section.
void wrapInContainer(llvm::ArrayRef<uint32_t> Bytecode, ShaderKind Kind,
                     llvm::SmallVectorImpl<char> &Out);

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_ENCODER_H
