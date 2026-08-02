//===- SPIRVImporter.cpp - SPIR-V binary importer ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/SPIRV/SPIRVImporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Target/SPIRV/Deserialization.h"
#include "llvm/Support/Error.h"

using namespace feme;

llvm::Expected<Module> SPIRVImporter::import(llvm::MemoryBufferRef Buffer,
                                             const ImportOptions &Opts,
                                             Context &Ctx) const {
  // SPIR-V binaries are a stream of 32-bit words (see the SPIR-V
  // specification); reject inputs that cannot possibly be one rather than
  // reading past the end of the buffer.
  if (Buffer.getBufferSize() % sizeof(uint32_t) != 0)
    return llvm::createStringError(
        llvm::inconvertibleErrorCode(),
        "SPIR-V binary module must contain an integral number of 32-bit "
        "words");

  mlir::MLIRContext &MLIRCtx = Ctx.getMLIRContext();
  MLIRCtx.loadDialect<mlir::spirv::SPIRVDialect>();

  auto Binary = llvm::ArrayRef(
      reinterpret_cast<const uint32_t *>(Buffer.getBufferStart()),
      Buffer.getBufferSize() / sizeof(uint32_t));

  mlir::spirv::DeserializationOptions DeserOpts;
  DeserOpts.enableControlFlowStructurization =
      Opts.SPIRVEnableControlFlowStructurization;

  mlir::OwningOpRef<mlir::spirv::ModuleOp> SpirvModule =
      mlir::spirv::deserialize(Binary, &MLIRCtx, DeserOpts);
  if (!SpirvModule)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "failed to deserialize SPIR-V module");

  return Module::fromMLIR(std::move(SpirvModule));
}

llvm::StringRef SPIRVImporter::getFormatName() const { return "spirv"; }
