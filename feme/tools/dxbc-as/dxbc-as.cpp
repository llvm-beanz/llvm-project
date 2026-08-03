//===- dxbc-as.cpp - Standalone DXBC assembler ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// dxbc-as assembles Microsoft/`fxc`-style DXBC disassembly text into raw
// DXBC tokenized shader bytecode (optionally wrapped in a DXContainer), or
// re-prints it as normalized assembly text -- see the "dxbc-as" section of
// feme/docs/Design.md. It has no dependency on MLIR, the `dxsa` dialect, or
// feme::Context (comparable in spirit to `llvm-mc`), and exists purely to
// produce human-readable, diffable DXBC test fixtures for a future DXBC
// importer, independent of the code that importer's own tests validate.
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/AsmPrinter.h"
#include "feme/DXBC/Assembler/Encoder.h"
#include "feme/DXBC/Assembler/Parser.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace feme::dxbc;

namespace {
enum class EmitKind { Binary, Container, Asm };
} // namespace

static cl::OptionCategory DxbcAsCategory("dxbc-as options");

static cl::opt<std::string> InputFilename(cl::Positional,
                                          cl::desc("<input DXBC assembly>"),
                                          cl::init("-"),
                                          cl::cat(DxbcAsCategory));

static cl::opt<std::string> OutputFilename("o", cl::desc("Output filename"),
                                           cl::value_desc("filename"),
                                           cl::init("-"),
                                           cl::cat(DxbcAsCategory));

static cl::opt<EmitKind> Emit(
    "emit", cl::desc("What to emit"),
    cl::values(clEnumValN(EmitKind::Binary, "binary",
                          "Raw DXBC tokenized shader bytecode (default)"),
               clEnumValN(EmitKind::Container, "container",
                          "Bytecode wrapped in a minimal DXContainer"),
               clEnumValN(EmitKind::Asm, "asm",
                          "Re-print parsed input as normalized assembly text")),
    cl::init(EmitKind::Binary), cl::cat(DxbcAsCategory));

static cl::opt<ShaderKind> Stage(
    "shader-kind", cl::desc("Shader stage to declare in the program header"),
    cl::values(clEnumValN(ShaderKind::Pixel, "pixel", "Pixel shader"),
               clEnumValN(ShaderKind::Vertex, "vertex", "Vertex shader"),
               clEnumValN(ShaderKind::Compute, "compute", "Compute shader")),
    cl::init(ShaderKind::Pixel), cl::cat(DxbcAsCategory));

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  cl::HideUnrelatedOptions(DxbcAsCategory);
  cl::ParseCommandLineOptions(argc, argv,
                              "dxbc-as: standalone DXBC assembler\n");

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
  if (std::error_code EC = BufferOrErr.getError()) {
    WithColor::error(errs(), "dxbc-as")
        << "could not open '" << InputFilename << "': " << EC.message() << '\n';
    return 1;
  }

  Expected<std::vector<Instruction>> Program =
      parseAssembly((*BufferOrErr)->getBuffer());
  if (!Program) {
    WithColor::error(errs(), "dxbc-as")
        << InputFilename << ": " << toString(Program.takeError()) << '\n';
    return 1;
  }

  bool Binary = Emit == EmitKind::Binary || Emit == EmitKind::Container;
  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC,
                     Binary ? sys::fs::OF_None : sys::fs::OF_TextWithCRLF);
  if (EC) {
    WithColor::error(errs(), "dxbc-as") << "could not open '" << OutputFilename
                                        << "': " << EC.message() << '\n';
    return 1;
  }

  if (Emit == EmitKind::Asm) {
    printAssembly(*Program, Out.os());
    Out.keep();
    return 0;
  }

  Expected<SmallVector<uint32_t, 64>> Bytecode = encodeProgram(*Program, Stage);
  if (!Bytecode) {
    WithColor::error(errs(), "dxbc-as")
        << InputFilename << ": " << toString(Bytecode.takeError()) << '\n';
    return 1;
  }

  if (Emit == EmitKind::Container) {
    SmallVector<char, 256> ContainerBytes;
    wrapInContainer(*Bytecode, Stage, ContainerBytes);
    Out.os().write(ContainerBytes.data(), ContainerBytes.size());
  } else {
    support::endian::Writer W(Out.os(), endianness::little);
    for (uint32_t Word : *Bytecode)
      W.write(Word);
  }
  Out.keep();
  return 0;
}
