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

JITEngine::JITEngine(std::unique_ptr<CompiledStage> Stage, unsigned NumThreads)
    : Stage(std::move(Stage)), NumThreads(NumThreads) {}

JITEngine::~JITEngine() = default;
JITEngine::JITEngine(JITEngine &&) noexcept = default;
JITEngine &JITEngine::operator=(JITEngine &&) noexcept = default;

Expected<std::unique_ptr<JITEngine>>
JITEngine::create(Context &Ctx, feme::Module M, const JITOptions &Opts) {
  Expected<std::unique_ptr<CompiledStage>> Stage =
      CompiledStage::create(Ctx, std::move(M), Opts);
  if (!Stage)
    return Stage.takeError();
  return std::unique_ptr<JITEngine>(
      new JITEngine(std::move(*Stage), Opts.NumThreads));
}

Error JITEngine::dispatch(const DispatchResources &Resources,
                          std::array<uint32_t, 3> GroupCount) const {
  PreparedDispatch Prepared =
      PreparedDispatch::create(Stage->getResourceInfo(), Resources, GroupCount);

  // `NumThreads == 1` runs every group on the calling thread, with no pool
  // set up at all -- both the cheapest option for a single-group dispatch
  // (the overwhelming majority of today's tests) and the behavior every
  // existing caller of this milestone's predecessor relied on.
  if (NumThreads == 1) {
    for (uint32_t Z = 0; Z != GroupCount[2]; ++Z)
      for (uint32_t Y = 0; Y != GroupCount[1]; ++Y)
        for (uint32_t X = 0; X != GroupCount[0]; ++X)
          if (Error E = Stage->invokeGroup(Prepared, {X, Y, Z},
                                           /*GroupShared=*/{}))
            return E;
    return Error::success();
  }

  // Groups are independent by definition (see "Dispatch parallelism" in
  // feme/docs/FeMeCPUDesign.md's "JIT Flow" section), so scheduling them
  // across a worker pool needs no synchronization beyond this join; each
  // `dispatch` call gets its own pool and task group so concurrent
  // dispatches against the same `JITEngine` never contend with each other.
  DefaultThreadPool Pool(llvm::hardware_concurrency(NumThreads));
  ThreadPoolTaskGroup Group(Pool);

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
