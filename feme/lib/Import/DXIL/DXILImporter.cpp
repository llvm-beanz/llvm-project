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
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"

using namespace feme;

/// DXContainers start with a "DXBC" magic (the format predates the DXIL
/// name; see llvm::object::DXContainer::parseHeader). Distinguishing the
/// two accepted encodings up front lets us give a clear diagnostic for
/// inputs that are neither, rather than an opaque bitcode-reader error.
static bool isDXContainer(llvm::MemoryBufferRef Buffer) {
  return Buffer.getBuffer().starts_with("DXBC");
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
  // DXIL-specific compatibility shim.
  llvm::Expected<std::unique_ptr<llvm::Module>> LLVMModule =
      llvm::parseBitcodeFile(BitcodeBuffer, Ctx.getLLVMContext());
  if (!LLVMModule)
    return LLVMModule.takeError();

  return Module::fromLLVMIR(std::move(*LLVMModule));
}

llvm::StringRef DXILImporter::getFormatName() const { return "dxil"; }
