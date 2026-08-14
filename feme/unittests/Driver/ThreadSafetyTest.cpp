//===- ThreadSafetyTest.cpp - Tests for feme::Context thread-safety ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Closes the P0 gap tracked in the "Core library plumbing" table of
// feme/docs/Roadmap.md §1.1: thread-safety (one feme::Context per thread,
// stateless components) is a claim made by the "Core Architectural
// Principle: No Global State" section of feme/docs/Design.md, but nothing
// verified it. This imports the same input on multiple threads, each using
// its own Context, through a single shared (stateless, statically-linked)
// DXILImporter instance and the DXIL raising passes -- exactly the "N
// threads, N Contexts" shape the Roadmap calls for -- and checks that
// nothing crashes or corrupts another thread's result. Run this test binary
// under ThreadSanitizer for the strongest form of this check (no shared
// mutable state, not just "did not crash").
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/DXIL/DXILImporter.h"
#include "feme/Transforms/DXIL/MetadataRaising.h"
#include "feme/Transforms/DXIL/OpRaising.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "gtest/gtest.h"

#include <memory>
#include <thread>
#include <vector>

using namespace feme;

namespace {

// Builds a small, valid LLVM bitcode module once, to be imported
// concurrently by every thread below. The buffer is read-only after this
// point, so sharing it across threads is not itself the property under
// test -- each thread's own feme::Context and the import/raise it drives
// through that Context is.
llvm::SmallVector<char, 0> buildTestBitcode() {
  llvm::LLVMContext BuildCtx;
  llvm::Module Source("thread-safety-test", BuildCtx);
  llvm::Function *F = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(BuildCtx), false),
      llvm::Function::ExternalLinkage, "main", Source);
  F->addFnAttr("hlsl.shader", "compute");
  F->addFnAttr("hlsl.numthreads", "1,1,1");
  llvm::BasicBlock *BB = llvm::BasicBlock::Create(BuildCtx, "entry", F);
  llvm::ReturnInst::Create(BuildCtx, BB);

  llvm::SmallVector<char, 0> Bitcode;
  llvm::raw_svector_ostream OS(Bitcode);
  llvm::WriteBitcodeToFile(Source, OS);
  return Bitcode;
}

TEST(ThreadSafetyTest, ConcurrentContextsImportAndRaiseIndependently) {
  llvm::SmallVector<char, 0> Bitcode = buildTestBitcode();
  llvm::MemoryBufferRef Input(
      llvm::StringRef(Bitcode.data(), Bitcode.size()), "thread-safety-test");

  // One statically-shaped Importer instance, shared (read-only) across
  // every thread: per "Core Architectural Principle: No Global State",
  // Importer implementations are stateless/reentrant precisely so the same
  // instance can be invoked concurrently, each thread passing its own
  // Context. Sharing one instance here is therefore the scenario under
  // test, not an artifact of it.
  const DXILImporter Importer;

  constexpr unsigned NumThreads = 8;
  constexpr unsigned IterationsPerThread = 25;

  // All Contexts are constructed up front and kept alive for the whole
  // test so that, unlike sequentially-created-and-destroyed Contexts,
  // pointer comparisons below cannot coincidentally alias through
  // allocator reuse.
  std::vector<std::unique_ptr<Context>> Contexts;
  Contexts.reserve(NumThreads);
  for (unsigned I = 0; I < NumThreads; ++I)
    Contexts.push_back(std::make_unique<Context>());

  std::vector<unsigned char> ThreadSucceeded(NumThreads, 0);
  std::vector<std::thread> Threads;
  Threads.reserve(NumThreads);
  for (unsigned T = 0; T < NumThreads; ++T) {
    Threads.emplace_back([&, T]() {
      Context &Ctx = *Contexts[T];
      bool AllOK = true;
      for (unsigned I = 0; I < IterationsPerThread; ++I) {
        llvm::Expected<Module> Result =
            Importer.import(Input, ImportOptions{}, Ctx);
        if (!Result) {
          llvm::consumeError(Result.takeError());
          AllOK = false;
          continue;
        }

        llvm::Module &M = Result->getLLVMModule();
        // The imported module must live in *this* thread's Context, never
        // another thread's -- a shared/racing LLVMContext would fail this.
        if (&M.getContext() != &Ctx.getLLVMContext())
          AllOK = false;

        llvm::ModuleAnalysisManager MAM;
        feme::dxil::OpRaisingPass().run(M, MAM);
        feme::dxil::MetadataRaisingPass().run(M, MAM);

        if (llvm::verifyModule(M, /*OS=*/nullptr))
          AllOK = false;
        if (!M.getFunction("main"))
          AllOK = false;
      }
      ThreadSucceeded[T] = AllOK;
    });
  }

  for (std::thread &Th : Threads)
    Th.join();

  for (unsigned T = 0; T < NumThreads; ++T)
    EXPECT_TRUE(ThreadSucceeded[T]) << "thread " << T << " saw a failure";

  // Independent Contexts must never share mutable state (see the "No
  // Global State" principle in feme/docs/Design.md): with every Context
  // still alive at this point, their underlying LLVMContexts/MLIRContexts
  // must all be distinct.
  llvm::SmallPtrSet<llvm::LLVMContext *, NumThreads> UniqueLLVMContexts;
  llvm::SmallPtrSet<mlir::MLIRContext *, NumThreads> UniqueMLIRContexts;
  for (std::unique_ptr<Context> &Ctx : Contexts) {
    UniqueLLVMContexts.insert(&Ctx->getLLVMContext());
    UniqueMLIRContexts.insert(&Ctx->getMLIRContext());
  }
  EXPECT_EQ(UniqueLLVMContexts.size(), NumThreads);
  EXPECT_EQ(UniqueMLIRContexts.size(), NumThreads);
}

} // namespace
