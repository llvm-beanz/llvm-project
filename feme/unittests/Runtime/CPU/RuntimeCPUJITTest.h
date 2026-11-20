//===- RuntimeCPUJITTest.h - Runtime helper JIT fixture -----------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FEME_UNITTESTS_RUNTIME_CPU_RUNTIMECPUJITTEST_H
#define FEME_UNITTESTS_RUNTIME_CPU_RUNTIMECPUJITTEST_H

#include "feme/Target/CPU/RuntimeCPU.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Testing/Support/Error.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "gtest/gtest.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>

namespace feme::cpu {

/// JITs a test's entry points and their transitive runtime dependencies.
class RuntimeCPUJITTest : public testing::Test {
public:
  ~RuntimeCPUJITTest() override;

protected:
  static void SetUpTestSuite() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
  }

  llvm::LLVMContext Ctx;
  llvm::Module *M = nullptr;
  std::unique_ptr<llvm::ExecutionEngine> Engine;

  void SetUp() override {
    auto ModOrErr = llvm::parseBitcodeFile(getRuntimeCPUBitcode(), Ctx);
    ASSERT_THAT_EXPECTED(ModOrErr, llvm::Succeeded());
    auto Owner = std::move(*ModOrErr);
    M = Owner.get();
    std::string Error;
    Engine.reset(
        llvm::EngineBuilder(std::move(Owner)).setErrorStr(&Error).create());
    ASSERT_TRUE(Engine) << Error;
  }

  static llvm::Function *getRuntimeFunction(llvm::Module &M,
                                            llvm::StringRef Name) {
    if (llvm::Function *F = M.getFunction(Name))
      return F;
    // Mach-O gives explicit asm labels a mangling-escape prefix.
    return M.getFunction(("\1" + Name).str());
  }

  llvm::Function *keepForJIT(llvm::Function *F) {
    assert(!Compiled &&
           "add all entry points before resolving their addresses");
    EntryPoints.insert(F);
    return F;
  }

  template <typename FnTy> FnTy resolve(llvm::Function *F) {
    if (!Compiled) {
      EntryPoints.insert(F);
      // MCJIT compiles every surviving definition. Internalize non-entry
      // helpers so GlobalDCE keeps only the actual test's dependency graph.
      for (llvm::Function &Helper : *M)
        if (!Helper.isDeclaration() && !EntryPoints.contains(&Helper))
          Helper.setLinkage(llvm::GlobalValue::InternalLinkage);
      llvm::ModuleAnalysisManager AM;
      llvm::GlobalDCEPass().run(*M, AM);
      Compiled = true;
    }
    uint64_t Addr = Engine->getFunctionAddress(F->getName().str());
    assert(Addr && "MCJIT failed to resolve a runtime function address");
    return reinterpret_cast<FnTy>(Addr);
  }

private:
  llvm::SmallPtrSet<llvm::Function *, 4> EntryPoints;
  bool Compiled = false;
};

} // namespace feme::cpu

#endif
