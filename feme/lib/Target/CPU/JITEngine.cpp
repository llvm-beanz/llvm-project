//===- JITEngine.cpp - FeMe CPU target JIT dispatch engine ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/JITEngine.h"

#include "feme/Core/Module.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"

#include <mutex>

using namespace llvm;
using namespace feme::cpu;

JITEngine::JITEngine(std::unique_ptr<CompiledStage> Stage,
                     std::unique_ptr<ThreadPoolInterface> Pool)
    : Stage(std::move(Stage)), Pool(std::move(Pool)) {}

JITEngine::~JITEngine() = default;
JITEngine::JITEngine(JITEngine &&) noexcept = default;
JITEngine &JITEngine::operator=(JITEngine &&) noexcept = default;

Expected<std::unique_ptr<JITEngine>>
JITEngine::create(Context &Ctx, feme::Module M, const JITOptions &Opts) {
  Expected<std::unique_ptr<CompiledStage>> Stage =
      CompiledStage::create(Ctx, std::move(M), Opts);
  if (!Stage)
    return Stage.takeError();

  // `NumThreads == 1` needs no pool at all: `dispatch` below runs every
  // group on the calling thread in that case. Otherwise the pool is owned
  // by the engine for its whole lifetime (see "Dispatch parallelism" in
  // feme/docs/FeMeCPUDesign.md's "JIT Flow" section), so repeated
  // `dispatch` calls reuse the same worker threads rather than spinning a
  // fresh pool up and down every time.
  std::unique_ptr<ThreadPoolInterface> Pool;
  if (Opts.NumThreads != 1)
    Pool = std::make_unique<DefaultThreadPool>(
        llvm::hardware_concurrency(Opts.NumThreads));

  return std::unique_ptr<JITEngine>(
      new JITEngine(std::move(*Stage), std::move(Pool)));
}

Error JITEngine::dispatch(const DispatchResources &Resources,
                          std::array<uint32_t, 3> GroupCount) const {
  PreparedDispatch Prepared =
      PreparedDispatch::create(Stage->getResourceInfo(), Resources, GroupCount);

  if (!Pool) {
    for (uint32_t Z = 0; Z != GroupCount[2]; ++Z)
      for (uint32_t Y = 0; Y != GroupCount[1]; ++Y)
        for (uint32_t X = 0; X != GroupCount[0]; ++X)
          if (Error E = Stage->invokeGroup(Prepared, {X, Y, Z},
                                           /*GroupShared=*/{}))
            return E;
    return Error::success();
  }

  // Groups are independent by definition, so scheduling them across the
  // shared pool needs no synchronization beyond this join; each `dispatch`
  // call gets its own `ThreadPoolTaskGroup` so concurrent dispatches
  // against the same `JITEngine` wait only for their own groups rather than
  // for unrelated work already queued on the shared pool (see "Concurrent
  // dispatches" in feme/docs/FeMeCPUDesign.md's "JIT Flow" section).
  ThreadPoolTaskGroup Group(*Pool);

  std::mutex ErrorMutex;
  bool HadError = false;
  std::string FirstErrorMessage;

  for (uint32_t Z = 0; Z != GroupCount[2]; ++Z)
    for (uint32_t Y = 0; Y != GroupCount[1]; ++Y)
      for (uint32_t X = 0; X != GroupCount[0]; ++X)
        Group.async([&, X, Y, Z] {
          Error E = Stage->invokeGroup(Prepared, {X, Y, Z},
                                       /*GroupShared=*/{});
          if (!E)
            return;
          std::string Message = toString(std::move(E));
          std::lock_guard<std::mutex> Lock(ErrorMutex);
          if (!HadError) {
            HadError = true;
            FirstErrorMessage = std::move(Message);
          }
        });
  Group.wait();

  if (HadError)
    return createStringError(inconvertibleErrorCode(), "%s",
                             FirstErrorMessage.c_str());
  return Error::success();
}
