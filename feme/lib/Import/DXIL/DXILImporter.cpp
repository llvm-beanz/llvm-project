//===- DXILImporter.cpp - DXIL bitcode/DXContainer importer --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/DXIL/DXILImporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/raw_ostream.h"

using namespace feme;

/// DXContainers start with a "DXBC" magic (the format predates the DXIL
/// name; see llvm::object::DXContainer::parseHeader). Distinguishing the
/// two accepted encodings up front lets us give a clear diagnostic for
/// inputs that are neither, rather than an opaque bitcode-reader error.
static bool isDXContainer(llvm::MemoryBufferRef Buffer) {
  return Buffer.getBuffer().starts_with("DXBC");
}

/// Fixes up a DXIL module's frozen, historical data layout string so it
/// parses under modern LLVM's stricter `DataLayout` rules, which reject
/// `i8:32` (a non-1-byte ABI alignment for `i8`) that real DXC-emitted DXIL
/// embeds -- see the deviation noted in DXILImporter.h. Forcing `i8`'s
/// alignment to 1 byte does not change what the layout represents: modern
/// LLVM does not support (and DXIL's own struct layouts do not rely on) any
/// other `i8` alignment, so this is a lossless normalization, not a
/// best-effort guess. Returns \p Layout unchanged if it does not contain the
/// pattern needing normalization, so this is a no-op for any (non-DXIL)
/// input that doesn't need it.
static std::string normalizeDXILDataLayout(llvm::StringRef Layout) {
  llvm::SmallVector<llvm::StringRef, 16> Components;
  Layout.split(Components, '-');

  std::string Result;
  llvm::raw_string_ostream OS(Result);
  llvm::ListSeparator Sep("-");
  for (llvm::StringRef Component : Components) {
    OS << Sep;
    llvm::StringRef ABIAlign = Component;
    if (ABIAlign.consume_front("i8:") && ABIAlign != "8")
      OS << "i8:8";
    else
      OS << Component;
  }
  return Result;
}

/// Unwraps \p Buffer (a `DXContainer`) down to the `llvm::MemoryBufferRef`
/// spanning its embedded DXIL bitcode part. Prefers the non-debug "DXIL"
/// part, falling back to the debug "ILDB" part (which also embeds a full
/// bitcode module) if that's all the container has.
static llvm::Expected<llvm::MemoryBufferRef>
getDXILBitcode(llvm::MemoryBufferRef Buffer) {
  llvm::Expected<llvm::object::DXContainer> Container =
      llvm::object::DXContainer::create(Buffer);
  if (!Container)
    return Container.takeError();

  const std::optional<llvm::object::DXContainer::DXILData> &Program =
      Container->getDXIL(/*Debug=*/false);
  const std::optional<llvm::object::DXContainer::DXILData> &DebugProgram =
      Container->getDXIL(/*Debug=*/true);
  const std::optional<llvm::object::DXContainer::DXILData> &DXIL =
      Program ? Program : DebugProgram;
  if (!DXIL)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "DXContainer does not contain a DXIL program part");

  return llvm::MemoryBufferRef(
      llvm::StringRef(DXIL->second, DXIL->first.Bitcode.Size),
      Buffer.getBufferIdentifier());
}

llvm::Expected<Module> DXILImporter::import(llvm::MemoryBufferRef Buffer,
                                            const ImportOptions &Opts,
                                            Context &Ctx) const {
  // No DXIL-specific ImportOptions exist yet (see feme/Import/Importer.h);
  // this parameter is unused for now.
  (void)Opts;

  llvm::MemoryBufferRef BitcodeBuffer = Buffer;

  if (isDXContainer(Buffer)) {
    llvm::Expected<llvm::MemoryBufferRef> DXIL = getDXILBitcode(Buffer);
    if (!DXIL)
      return DXIL.takeError();
    BitcodeBuffer = *DXIL;
  } else if (!llvm::isBitcode(reinterpret_cast<const unsigned char *>(
                                  Buffer.getBufferStart()),
                              reinterpret_cast<const unsigned char *>(
                                  Buffer.getBufferEnd()))) {
    // Not a DXContainer and not bitcode either: reject up front rather than
    // reading past the end of the buffer or handing the bitcode reader
    // something it was never going to accept.
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "input is neither a DXContainer nor LLVM bitcode");
  }

  // DXIL bitcode is frozen at an old LLVM IR version, but LLVM's bitcode
  // reader auto-upgrades old bitcode on read (see "Bitcode parsing" in the
  // DXIL section of feme/docs/Design.md), so this does not need any
  // DXIL-specific compatibility shim for the IR itself. Its embedded data
  // layout string does need one (see normalizeDXILDataLayout above), which
  // is why this parses with an explicit DataLayoutCallback rather than
  // taking whatever layout string the bitcode reader would otherwise use
  // unmodified.
  llvm::Expected<std::unique_ptr<llvm::Module>> LLVMModule =
      llvm::parseBitcodeFile(
          BitcodeBuffer, Ctx.getLLVMContext(),
          llvm::ParserCallbacks([](llvm::StringRef, llvm::StringRef Layout)
                                    -> std::optional<std::string> {
            return normalizeDXILDataLayout(Layout);
          }));
  if (!LLVMModule)
    return LLVMModule.takeError();

  return Module::fromLLVMIR(std::move(*LLVMModule));
}

llvm::StringRef DXILImporter::getFormatName() const { return "dxil"; }
