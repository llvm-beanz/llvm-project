//===-- llvm/Bitcode/ReaderWriter.h - Bitcode reader/writers ----*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This header defines interfaces to read and write LLVM bitcode files/streams.
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_BITCODE_READERWRITER_H
#define DXIL_BITCODE_READERWRITER_H

#include "dxil/IR/DiagnosticInfo.h"
#include "dxil/Bitcode/BitcodeHeader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>
#include <string>

namespace dxil {
  class BitstreamWriter;
  class DataStreamer;
  class LLVMContext;
  class Module;
  class raw_ostream;

  /// Read the header of the specified bitcode buffer and prepare for lazy
  /// deserialization of function bodies. If ShouldLazyLoadMetadata is true,
  /// lazily load metadata as well. If successful, this moves Buffer. On
  /// error, this *does not* move Buffer.
  ErrorOr<std::unique_ptr<Module>>
  getLazyBitcodeModule(std::unique_ptr<MemoryBuffer> &&Buffer,
                       LLVMContext &Context,
                       DiagnosticHandlerFunction DiagnosticHandler = nullptr,
                       bool ShouldLazyLoadMetadata = false,
                       bool ShouldTrackBitstreamUsage = false);

  /// Read the header of the specified stream and prepare for lazy
  /// deserialization and streaming of function bodies.
  ErrorOr<std::unique_ptr<Module>> getStreamedBitcodeModule(
      StringRef Name, std::unique_ptr<DataStreamer> Streamer,
      LLVMContext &Context,
      DiagnosticHandlerFunction DiagnosticHandler = nullptr);

  /// Read the header of the specified bitcode buffer and extract just the
  /// triple information. If successful, this returns a string. On error, this
  /// returns "".
  std::string
  getBitcodeTargetTriple(MemoryBufferRef Buffer, LLVMContext &Context,
                         DiagnosticHandlerFunction DiagnosticHandler = nullptr);

  /// Read the specified bitcode file, returning the module.
  ErrorOr<std::unique_ptr<Module>>
  parseBitcodeFile(MemoryBufferRef Buffer, LLVMContext &Context,
                   DiagnosticHandlerFunction DiagnosticHandler = nullptr,
                   bool ShouldTrackBitstreamUsage = false); // HLSL Change

  /// \brief Write the specified module to the specified raw output stream.
  ///
  /// For streams where it matters, the given stream should be in "binary"
  /// mode.
  ///
  /// If \c ShouldPreserveUseListOrder, encode the use-list order for each \a
  /// Value in \c M.  These will be reconstructed exactly when \a M is
  /// deserialized.
  void WriteBitcodeToFile(const Module *M, raw_ostream &Out,
                          bool ShouldPreserveUseListOrder = false);

  const std::error_category &BitcodeErrorCategory();
  enum class BitcodeError { InvalidBitcodeSignature = 1, CorruptedBitcode };
  inline std::error_code make_error_code(BitcodeError E) {
    return std::error_code(static_cast<int>(E), BitcodeErrorCategory());
  }

  class BitcodeDiagnosticInfo : public DiagnosticInfo {
    const Twine &Msg;
    std::error_code EC;

  public:
    BitcodeDiagnosticInfo(std::error_code EC, DiagnosticSeverity Severity,
                          const Twine &Msg);
    void print(DiagnosticPrinter &DP) const override;
    std::error_code getError() const { return EC; };

    static bool classof(const DiagnosticInfo *DI) {
      return DI->getKind() == DK_Bitcode;
    }
  };

} // End llvm namespace

namespace std {
template <> struct is_error_code_enum<dxil::BitcodeError> : std::true_type {};
}

#endif
