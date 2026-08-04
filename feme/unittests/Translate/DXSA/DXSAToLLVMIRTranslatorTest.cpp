//===- DXSAToLLVMIRTranslatorTest.cpp - unit tests ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit tests for the dxsa dialect -> DXIL translation. The end-to-end
// DXBC-assembly-in, DXIL-out behaviour is covered by the lit tests under
// feme/test/Translate/DXBC; these cases pin down the pieces that are
// awkward to observe from there: the translation's diagnostics, and the
// module-level metadata a DXIL consumer needs.
//
//===----------------------------------------------------------------------===//

#include "feme/Translate/DXSA/DXSAToLLVMIRTranslator.h"

#include "feme/Dialect/DXSA/IR/DXSA.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"

#include "gtest/gtest.h"

using namespace feme;

namespace {

/// A parsed `dxsa` module plus everything it borrows, so that a test can
/// keep it alive for the duration of a translation.
class Fixture {
public:
  Fixture() {
    MLIR.getOrLoadDialect<dxsa::DXSADialect>();
    MLIR.getDiagEngine().registerHandler([this](mlir::Diagnostic &Diag) {
      Diagnostics += Diag.str();
      Diagnostics += '\n';
    });
  }

  /// Parses \p Source and translates it, returning the printed LLVM IR.
  /// Returns nullopt if either step failed.
  std::optional<std::string> translate(llvm::StringRef Source) {
    Parsed = mlir::parseSourceString<mlir::ModuleOp>(Source, &MLIR);
    if (!Parsed)
      return std::nullopt;
    std::unique_ptr<llvm::Module> Result =
        dxsa::translateToLLVMIR(*Parsed, LLVM);
    if (!Result)
      return std::nullopt;
    std::string Text;
    llvm::raw_string_ostream OS(Text);
    Result->print(OS, /*AAW=*/nullptr);
    return Text;
  }

  llvm::StringRef diagnostics() const { return Diagnostics; }

private:
  mlir::MLIRContext MLIR;
  llvm::LLVMContext LLVM;
  mlir::OwningOpRef<mlir::ModuleOp> Parsed;
  std::string Diagnostics;
};

constexpr llvm::StringRef PassthroughShader = R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_input_ps linear v<0, <x, y>>
  dxsa.dcl_output o<0, <x, y>>
  dxsa.mov o<0, <x, y>>, v<0, <x, y, x, x>>
  dxsa.ret
}
)mlir";

TEST(DXSAToLLVMIRTranslatorTest, RequiresExactlyOneShaderModule) {
  Fixture Empty;
  EXPECT_FALSE(Empty.translate("module { }").has_value());
  EXPECT_NE(Empty.diagnostics().find("expected a dxsa.module"),
            llvm::StringRef::npos);

  Fixture Two;
  EXPECT_FALSE(Two.translate("dxsa.module { }\ndxsa.module { }").has_value());
  EXPECT_NE(Two.diagnostics().find("expected exactly one dxsa.module"),
            llvm::StringRef::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, EmitsAnEntryPointAndModuleMetadata) {
  Fixture F;
  std::optional<std::string> IR = F.translate(PassthroughShader);
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();

  EXPECT_NE(IR->find("define void @main()"), std::string::npos);
  // A DXIL consumer keys off these named metadata nodes.
  EXPECT_NE(IR->find("!dx.version = !"), std::string::npos);
  EXPECT_NE(IR->find("!dx.shaderModel = !"), std::string::npos);
  EXPECT_NE(IR->find("!dx.entryPoints = !"), std::string::npos);
  EXPECT_NE(IR->find(R"(!{!"ps", i32 6, i32 0})"), std::string::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, ShaderModelNamesTheStage) {
  for (auto [Source, Stage] :
       {std::pair{"dxsa.module vertex_shader 5 0 { dxsa.ret }", "vs"},
        {"dxsa.module geometry_shader 5 0 { dxsa.ret }", "gs"},
        {"dxsa.module hull_shader 5 0 { dxsa.ret }", "hs"},
        {"dxsa.module domain_shader 5 0 { dxsa.ret }", "ds"},
        {"dxsa.module compute_shader 5 0 { dxsa.ret }", "cs"}}) {
    Fixture F;
    std::optional<std::string> IR = F.translate(Source);
    ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
    EXPECT_NE(IR->find((llvm::Twine("!{!\"") + Stage + "\", i32 6, i32 0}")
                           .str()),
              std::string::npos)
        << Source;
  }
}

TEST(DXSAToLLVMIRTranslatorTest, HeaderlessModuleDefaultsToPixelShader) {
  Fixture F;
  std::optional<std::string> IR = F.translate("dxsa.module { dxsa.ret }");
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();
  EXPECT_NE(IR->find(R"(!{!"ps", i32 6, i32 0})"), std::string::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, SignatureElementsComeFromDeclarations) {
  Fixture F;
  std::optional<std::string> IR = F.translate(PassthroughShader);
  ASSERT_TRUE(IR.has_value()) << F.diagnostics().str();

  // One element per declaration: two columns starting at column zero of
  // row zero, an arbitrary (0) input semantic and a render target (16)
  // output semantic.
  EXPECT_NE(IR->find(R"(!{i32 0, !"IN0", i8 9, i8 0,)"), std::string::npos);
  EXPECT_NE(IR->find(R"(!{i32 0, !"SV_Target", i8 9, i8 16,)"),
            std::string::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, ReportsUnsupportedInstructions) {
  Fixture F;
  // `dxsa.sample` needs resource handles, which are not translated yet.
  EXPECT_FALSE(F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_output o<0, <x>>
  dxsa.sample o<0, <x>>, v<0>, t<0>, s<0>
  dxsa.ret
}
)mlir")
                   .has_value());
  EXPECT_NE(F.diagnostics().find("does not support"), llvm::StringRef::npos);
}

TEST(DXSAToLLVMIRTranslatorTest, ReportsUndeclaredSignatureRegisters) {
  Fixture F;
  EXPECT_FALSE(F.translate(R"mlir(
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_output o<0, <x>>
  dxsa.mov o<0, <x>>, v<3, <x>>
  dxsa.ret
}
)mlir")
                   .has_value());
  EXPECT_NE(F.diagnostics().find("undeclared input register"),
            llvm::StringRef::npos);
}

} // namespace
