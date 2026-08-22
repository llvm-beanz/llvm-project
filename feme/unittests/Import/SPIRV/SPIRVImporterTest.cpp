//===- SPIRVImporterTest.cpp - Tests for feme::SPIRVImporter -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/SPIRV/SPIRVImporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <cstring>
#include <string>
#include <vector>

using namespace feme;

namespace {

// Note: the "imports a valid SPIR-V binary into a `spirv.module`" case is
// deliberately not covered here. It requires building a real serialized
// SPIR-V binary, which is exactly the kind of binary-format round trip
// `feme-translate` exists to exercise via `lit`/`FileCheck` instead (see
// "Testing Strategy" in feme/docs/Design.md); see
// `test/Import/SPIRV/spirv-import.mlir`.

TEST(SPIRVImporterTest, GetFormatName) {
  SPIRVImporter Importer;
  EXPECT_EQ(Importer.getFormatName(), "spirv");
}

TEST(SPIRVImporterTest, RejectsNonWordAlignedInput) {
  Context Ctx;
  SPIRVImporter Importer;
  // 3 bytes: not a multiple of 4, so cannot be a stream of SPIR-V words.
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef("abc", "spirv-test"), ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(SPIRVImporterTest, RejectsMalformedBinary) {
  Context Ctx;
  SPIRVImporter Importer;
  // 4 bytes of garbage: word-aligned, but not a valid SPIR-V module (wrong
  // magic number), so deserialization itself must fail.
  llvm::Expected<Module> Result =
      Importer.import(llvm::MemoryBufferRef(
                          llvm::StringRef("\xde\xad\xbe\xef", 4), "spirv-test"),
                      ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

/// A minimal SPIR-V hand-assembler for the one test below that needs an
/// `OpExtInst` referencing an extended-instruction-set name MLIR's own
/// `spirv` dialect has no representation for at all (`NonSemantic.*`, per
/// the SPIR-V specification's own "instruction sets with no semantic
/// effect" convention) -- unlike every other importer test in this file,
/// this cannot be produced by `feme-translate`'s serialize/import round
/// trip (see `test/Import/SPIRV/spirv-import.mlir`'s own comment), since
/// there is no `spirv` dialect op to author it with in the first place.
class RawSPIRVModuleBuilder {
public:
  RawSPIRVModuleBuilder() {
    // The 5-word SPIR-V module header: magic number, version 1.0, a
    // generator magic number (unused by this ICD), an initial <id> bound
    // (raised as new <id>s are allocated below), and a reserved schema
    // word.
    Words = {0x07230203, 0x00010000, 0, 1, 0};
  }

  uint32_t nextId() { return Bound++; }

  void emit(uint32_t Opcode, llvm::ArrayRef<uint32_t> Operands) {
    Words.push_back(((static_cast<uint32_t>(Operands.size()) + 1) << 16) |
                    Opcode);
    Words.append(Operands.begin(), Operands.end());
  }

  /// Encodes \p Str as a SPIR-V "Literal String" (a null-terminated,
  /// zero-padded run of 32-bit words, each holding up to 4 UTF-8 bytes
  /// little-endian).
  static std::vector<uint32_t> literalString(llvm::StringRef Str) {
    std::string Bytes = Str.str();
    Bytes.push_back('\0');
    while (Bytes.size() % 4 != 0)
      Bytes.push_back('\0');
    std::vector<uint32_t> Result;
    for (size_t I = 0; I != Bytes.size(); I += 4) {
      uint32_t Word;
      std::memcpy(&Word, Bytes.data() + I, 4);
      Result.push_back(Word);
    }
    return Result;
  }

  std::vector<uint32_t> finish() {
    Words[3] = Bound;
    return std::vector<uint32_t>(Words.begin(), Words.end());
  }

private:
  llvm::SmallVector<uint32_t> Words;
  uint32_t Bound = 1;
};

/// A minimal `void main()` module whose one instruction is an `OpExtInst`
/// call into a `NonSemantic.DebugPrintf`-named extended instruction set
/// (`VK_KHR_shader_non_semantic_info`, roadmap E19) -- exactly the shape
/// `SPIRVImporter.cpp`'s `stripNonSemanticExtInst` must strip before MLIR's
/// deserializer (which has no case for this set name at all) ever sees it.
std::vector<uint32_t> buildNonSemanticExtInstModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t ExtSet = B.nextId();
  uint32_t ExtResult = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  {
    std::vector<uint32_t> Operands{ExtSet};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString(
                                     "NonSemantic.DebugPrintf"));
    B.emit(/*OpExtInstImport=*/11, Operands);
  }
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*Vertex=*/0, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  // `%ExtResult = OpExtInst %Void %ExtSet 1` -- instruction number 1 of
  // `NonSemantic.DebugPrintf`, with no operands of its own.
  B.emit(/*OpExtInst=*/12, {Void, ExtResult, ExtSet, /*Instruction=*/1});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

TEST(SPIRVImporterTest, StripsNonSemanticExtInst) {
  Context Ctx;
  SPIRVImporter Importer;
  std::vector<uint32_t> Words = buildNonSemanticExtInstModule();
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef(
          llvm::StringRef(reinterpret_cast<const char *>(Words.data()),
                          Words.size() * sizeof(uint32_t)),
          "spirv-test"),
      ImportOptions{}, Ctx);
  // Without stripping, this fails: MLIR's deserializer has no case for the
  // `NonSemantic.DebugPrintf` extended-instruction-set name at all (see
  // `stripNonSemanticExtInst`'s own comment).
  EXPECT_THAT_EXPECTED(Result, llvm::Succeeded());
}

} // namespace
