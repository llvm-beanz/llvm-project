//===- DXBCImporter.cpp - legacy DXBC container importer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/DXBC/DXBCImporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Dialect/DXSA/IR/DXSA.h"
#include "feme/Target/DXSA/BinaryParser.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/SourceMgr.h"

using namespace feme;

/// A legacy DXBC container stores its tokenized shader bytecode in an
/// `SHEX` part (Shader Model 4.1+, extended opcode tokens) or an `SHDR`
/// part (Shader Model 4.0, no extended tokens) -- see
/// `feme::dxbc::wrapInContainer` and the "Building complete legacy DXBC
/// containers for testing" section of feme/docs/Design.md. Either name
/// identifies the same thing `feme::dxsa::deserialize` needs: the raw
/// tokenized instruction stream, program header included.
static llvm::Expected<llvm::StringRef>
getShaderBytecode(const llvm::object::DXContainer &Container) {
  // Guard the loop below rather than calling Container.begin() on a
  // zero-part container: llvm::object::DXContainer::PartIterator's
  // constructor unconditionally reads PartOffsets.back() when begin()
  // already equals end(), which is only safe when at least one part
  // offset exists.
  if (Container.getHeader().PartCount != 0)
    for (const auto &Part : Container) {
      llvm::StringRef Name = Part.Part.getName();
      if (Name == "SHEX" || Name == "SHDR")
        return Part.Data;
    }

  return llvm::createStringError(
      llvm::inconvertibleErrorCode(),
      "DXContainer does not contain a DXBC shader bytecode ('SHEX' or "
      "'SHDR') part");
}

llvm::Expected<Module> DXBCImporter::import(llvm::MemoryBufferRef Buffer,
                                            const ImportOptions &Opts,
                                            Context &Ctx) const {
  // No DXBC-specific ImportOptions exist yet (see feme/Import/Importer.h);
  // this parameter is unused for now.
  (void)Opts;

  llvm::Expected<llvm::object::DXContainer> Container =
      llvm::object::DXContainer::create(Buffer);
  if (!Container)
    return Container.takeError();

  llvm::Expected<llvm::StringRef> Bytecode = getShaderBytecode(*Container);
  if (!Bytecode)
    return Bytecode.takeError();

  llvm::SourceMgr SrcMgr;
  SrcMgr.AddNewSourceBuffer(
      llvm::MemoryBuffer::getMemBuffer(*Bytecode, Buffer.getBufferIdentifier(),
                                       /*RequiresNullTerminator=*/false),
      llvm::SMLoc());

  // Unlike SPIR-V import, feme::dxsa::deserialize does not itself require
  // its dialect to already be loaded (BinaryParser.cpp works around
  // Context's not registering any MLIR dialects up front -- see the "No
  // FormatRegistry" gap in feme/docs/Roadmap.md -- via
  // `loadAllAvailableDialects()`), but that only loads dialects already
  // present in the MLIRContext's registry, not ones that were never
  // registered at all. Load DXSADialect explicitly so a freshly
  // constructed feme::Context (as `feme` itself uses, unlike
  // feme-translate's MlirTranslateMain, which pre-populates its registry
  // from every registered translation's dialect hook) works too.
  Ctx.getMLIRContext().getOrLoadDialect<dxsa::DXSADialect>();

  mlir::OwningOpRef<dxsa::ModuleOp> Parsed =
      dxsa::deserialize(SrcMgr, &Ctx.getMLIRContext());
  if (!Parsed)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "failed to parse DXBC shader bytecode into the dxsa dialect");

  return Module::fromMLIR(std::move(Parsed));
}

llvm::StringRef DXBCImporter::getFormatName() const { return "dxbc"; }
