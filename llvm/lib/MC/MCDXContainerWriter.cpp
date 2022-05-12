//===- llvm/MC/MCDXContainerWriter.cpp - DXContainer Writer -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCDXContainerWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/EndianStream.h"

using namespace llvm;

namespace {
class DXContainerObjectWriter : public MCObjectWriter {
  ::support::endian::Writer W;

  /// The target specific DXContainer writer instance.
  std::unique_ptr<MCDXContainerTargetWriter> TargetObjectWriter;

public:
  DXContainerObjectWriter(std::unique_ptr<MCDXContainerTargetWriter> MOTW,
                          raw_pwrite_stream &OS)
      : W(OS, support::little), TargetObjectWriter(std::move(MOTW)) {}

  ~DXContainerObjectWriter() override {}

private:
  void recordRelocation(MCAssembler &Asm, const MCAsmLayout &Layout,
                        const MCFragment *Fragment, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override {}

  void executePostLayoutBinding(MCAssembler &Asm,
                                const MCAsmLayout &Layout) override {}

  uint64_t writeObject(MCAssembler &Asm, const MCAsmLayout &Layout) override {
    return 0;
  }
};
} // namespace

std::unique_ptr<MCObjectWriter> llvm::createDXContainerObjectWriter(
    std::unique_ptr<MCDXContainerTargetWriter> MOTW, raw_pwrite_stream &OS) {
  return std::make_unique<DXContainerObjectWriter>(std::move(MOTW), OS);
}
