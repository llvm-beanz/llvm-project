//===- Parser.h - DXBC assembler parser ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares parseAssembly, the second stage of dxbc-as's lex -> parse ->
// encode pipeline (see feme/docs/Design.md's "dxbc-as" section): it turns
// DXBC assembly text into the Program the Encoder and AsmPrinter consume.
//
//===----------------------------------------------------------------------===//

#ifndef FEME_DXBC_ASSEMBLER_PARSER_H
#define FEME_DXBC_ASSEMBLER_PARSER_H

#include "feme/DXBC/Assembler/Instruction.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <vector>

namespace feme {
namespace dxbc {

/// One parsed DXBC assembly translation unit: an optional program header
/// (requested by a `.shader_model` directive) plus the instruction stack.
struct Program {
  /// True if the source asked for a version/length token pair to precede
  /// the instructions. Shader bodies extracted from a DXContainer's SHEX
  /// part always have one; hand-written instruction-sequence fixtures
  /// usually do not, so it is opt-in.
  bool HasHeader = false;
  /// D3D10_SB_TOKENIZED_PROGRAM_TYPE of the header, when \c HasHeader.
  uint16_t ProgramType = 0;
  uint8_t MajorVersion = 5;
  uint8_t MinorVersion = 0;

  std::vector<Instruction> Instructions;
};

/// Parses \p Source as DXBC assembly text.
llvm::Expected<Program> parseAssembly(llvm::StringRef Source);

} // namespace dxbc
} // namespace feme

#endif // FEME_DXBC_ASSEMBLER_PARSER_H
