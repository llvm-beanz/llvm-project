//===- BitcodeTest.cpp - Legacy bitcode reader and writer tests
//-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/AsmParser/Parser.h"
#include "dxil/Bitcode/LLVMBitCodes.h"
#include "dxil/Bitcode/ReaderWriter.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/DiagnosticPrinter.h"
#include "dxil/IR/LLVMContext.h"
#include "dxil/IR/Module.h"
#include "dxil/IR/Verifier.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace dxil;

namespace {

class BitcodeTest : public testing::Test {
protected:
  LLVMContext Context;
  llvm::SmallVector<char, 0> Bytes;

  void SetUp() override {
    llvm::SMDiagnostic Error;
    auto M = parseAssemblyString(R"(
      target triple = "dxil-pc-shadermodel6.0-compute"
      @value = global i32 42
      define i32 @first(i32* %ptr) {
        %value = load i32, i32* %ptr
        ret i32 %value
      }
      define i32 @second() { ret i32 7 }
      !test = !{!0}
      !0 = distinct !{!0, !"legacy", i32 42}
    )",
                                 Error, Context);
    ASSERT_TRUE(M) << Error.getMessage().str();
    ASSERT_FALSE(verifyModule(*M));
    llvm::raw_svector_ostream OS(Bytes);
    WriteBitcodeToFile(M.get(), OS, true);
  }
};

TEST_F(BitcodeTest, EagerRoundTrip) {
  LLVMContext ReadContext;
  auto M = parseBitcodeFile(
      llvm::MemoryBufferRef(llvm::StringRef(Bytes.data(), Bytes.size()),
                            "legacy"),
      ReadContext);
  ASSERT_TRUE(M) << M.getError().message();
  EXPECT_FALSE(verifyModule(**M));
  EXPECT_EQ(nullptr, (*M)->getMaterializer());
  EXPECT_EQ("dxil-pc-shadermodel6.0-compute", (*M)->getTargetTriple());
  EXPECT_EQ(1u, (*M)->getFunction("first")->size());
  auto *MD = (*M)->getNamedMetadata("test")->getOperand(0);
  EXPECT_EQ(MD, MD->getOperand(0));
  EXPECT_EQ("legacy", llvm::cast<MDString>(MD->getOperand(1))->getString());

  llvm::SmallVector<char, 0> RoundTrip;
  llvm::raw_svector_ostream OS(RoundTrip);
  WriteBitcodeToFile(M->get(), OS, true);
  EXPECT_EQ(Bytes, RoundTrip);
}

TEST_F(BitcodeTest, LazyFunctionsAndMetadata) {
  LLVMContext ReadContext;
  auto Buffer = llvm::MemoryBuffer::getMemBufferCopy(
      llvm::StringRef(Bytes.data(), Bytes.size()), "lazy");
  auto M = getLazyBitcodeModule(std::move(Buffer), ReadContext, nullptr, true);
  ASSERT_TRUE(M) << M.getError().message();
  EXPECT_EQ(nullptr, Buffer);
  auto *First = (*M)->getFunction("first");
  auto *Second = (*M)->getFunction("second");
  ASSERT_NE(nullptr, First);
  ASSERT_NE(nullptr, Second);
  EXPECT_TRUE(First->empty());
  EXPECT_TRUE(Second->empty());
  EXPECT_TRUE(First->isMaterializable());
  EXPECT_TRUE(Second->isMaterializable());
  EXPECT_EQ(nullptr, (*M)->getNamedMetadata("test"));

  EXPECT_FALSE((*M)->materializeMetadata());
  ASSERT_NE(nullptr, (*M)->getNamedMetadata("test"));
  EXPECT_TRUE(First->empty());
  EXPECT_TRUE(Second->empty());
  EXPECT_FALSE(First->materialize());
  EXPECT_FALSE(First->empty());
  EXPECT_TRUE(Second->empty());
  EXPECT_TRUE(First->isDematerializable());
  First->dematerialize();
  EXPECT_TRUE(First->empty());
  EXPECT_TRUE(First->isMaterializable());
  EXPECT_FALSE(First->materialize());
  EXPECT_FALSE(verifyFunction(*First));
  EXPECT_FALSE((*M)->materializeAllPermanently());
  EXPECT_EQ(nullptr, (*M)->getMaterializer());
  EXPECT_FALSE(Second->empty());
  EXPECT_FALSE(verifyModule(**M));
}

TEST_F(BitcodeTest, ReadsDarwinWrapper) {
  LLVMContext WriteContext;
  llvm::SMDiagnostic Error;
  auto M = parseAssemblyString("target triple = \"x86_64-apple-darwin\"\n"
                               "define i32 @f() { ret i32 42 }\n",
                               Error, WriteContext);
  ASSERT_TRUE(M) << Error.getMessage().str();
  llvm::SmallVector<char, 0> Wrapped;
  llvm::raw_svector_ostream OS(Wrapped);
  WriteBitcodeToFile(M.get(), OS);
  auto *Start = reinterpret_cast<const unsigned char *>(Wrapped.data());
  EXPECT_TRUE(isBitcodeWrapper(Start, Start + Wrapped.size()));
  LLVMContext ReadContext;
  auto Read = parseBitcodeFile(
      llvm::MemoryBufferRef(llvm::StringRef(Wrapped.data(), Wrapped.size()),
                            "wrapped"),
      ReadContext);
  ASSERT_TRUE(Read) << Read.getError().message();
  EXPECT_FALSE(verifyModule(**Read));
  EXPECT_EQ("x86_64-apple-darwin", (*Read)->getTargetTriple());
}

TEST_F(BitcodeTest, ExtractsTargetTriple) {
  LLVMContext ReadContext;
  EXPECT_EQ("dxil-pc-shadermodel6.0-compute",
            getBitcodeTargetTriple(
                llvm::MemoryBufferRef(
                    llvm::StringRef(Bytes.data(), Bytes.size()), "triple"),
                ReadContext));
}

TEST(Bitcode, WarnsAboutUnknownSubblocks) {
  llvm::SmallVector<char, 0> Bytes;
  {
    llvm::BitstreamWriter Stream(Bytes);
    Stream.Emit(0xDEC04342, 32);
    Stream.EnterSubblock(bitc::MODULE_BLOCK_ID, 3);
    llvm::SmallVector<unsigned, 1> Version{1};
    Stream.EmitRecord(bitc::MODULE_CODE_VERSION, Version);
    Stream.EnterSubblock(bitc::PARAMATTR_BLOCK_ID, 3);
    Stream.EnterSubblock(999, 2);
    Stream.ExitBlock();
    Stream.ExitBlock();
    Stream.ExitBlock();
  }
  LLVMContext Context;
  unsigned Warnings = 0;
  auto M = parseBitcodeFile(
      llvm::MemoryBufferRef(llvm::StringRef(Bytes.data(), Bytes.size()),
                            "unknown-subblock"),
      Context, [&](const DiagnosticInfo &DI) {
        EXPECT_EQ(DK_Bitcode, DI.getKind());
        EXPECT_EQ(DS_Warning, DI.getSeverity());
        ++Warnings;
      });
  ASSERT_TRUE(M) << M.getError().message();
  EXPECT_EQ(1u, Warnings);
}

TEST(Bitcode, ReportsBitstreamErrorsWithoutFatalErrorHandler) {
  LLVMContext Context;
  std::string Message;
  auto Handler = [&](const DiagnosticInfo &DI) {
    EXPECT_EQ(DK_Bitcode, DI.getKind());
    EXPECT_EQ(DS_Error, DI.getSeverity());
    Message.clear();
    llvm::raw_string_ostream OS(Message);
    DiagnosticPrinterRawOStream Printer(OS);
    DI.print(Printer);
  };

  for (llvm::StringRef Input : {llvm::StringRef(), llvm::StringRef("nope"),
                                llvm::StringRef("BC\xc0\xde", 4)}) {
    auto M = parseBitcodeFile(llvm::MemoryBufferRef(Input, "invalid"), Context,
                              Handler);
    EXPECT_FALSE(M);
    EXPECT_FALSE(Message.empty());
  }

  llvm::SmallVector<char, 0> Bytes;
  {
    llvm::BitstreamWriter Stream(Bytes);
    Stream.Emit(0xDEC04342, 32);
    Stream.EnterSubblock(bitc::MODULE_BLOCK_ID, 3);
    llvm::SmallVector<unsigned, 1> Version{1};
    Stream.EmitRecord(bitc::MODULE_CODE_VERSION, Version);
    Stream.ExitBlock();
  }
  // Preserve the block header but truncate its body.
  Bytes.resize(Bytes.size() - 4);
  auto M = parseBitcodeFile(
      llvm::MemoryBufferRef(llvm::StringRef(Bytes.data(), Bytes.size()),
                            "truncated"),
      Context, Handler);
  EXPECT_FALSE(M);
  EXPECT_FALSE(Message.empty());

  Bytes.clear();
  {
    llvm::BitstreamWriter Stream(Bytes);
    Stream.Emit(0xDEC04342, 32);
    Stream.EnterSubblock(bitc::MODULE_BLOCK_ID, 3);
    Stream.Emit(7, 3);
    Stream.ExitBlock();
  }
  M = parseBitcodeFile(
      llvm::MemoryBufferRef(llvm::StringRef(Bytes.data(), Bytes.size()),
                            "invalid-abbreviation"),
      Context, Handler);
  EXPECT_FALSE(M);
  EXPECT_NE(std::string::npos, Message.find("Invalid abbrev number"));
}

} // namespace
