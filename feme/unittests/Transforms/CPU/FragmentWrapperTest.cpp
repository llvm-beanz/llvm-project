//===- FragmentWrapperTest.cpp - Tests for FragmentWrapperPass ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Transforms/CPU/FragmentWrapper.h"

#include "feme/Core/Signature.h"
#include "feme/Core/StageOps.h"
#include "feme/Transforms/CPU/Linearize.h"
#include "llvm/IR/Constants.h"
#include "feme/Transforms/CPU/SIMDize.h"
#include "feme/Transforms/CPU/WaveLowering.h"
#include "feme/Transforms/DXIL/SignatureImport.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/SourceMgr.h"
#include "gtest/gtest.h"

using namespace feme;
using namespace feme::cpu;
using namespace llvm;

namespace {

std::unique_ptr<Module> parseIR(LLVMContext &Ctx, StringRef Assembly) {
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseAssemblyString(Assembly, Err, Ctx);
  if (!M)
    Err.print("FragmentWrapperTest", errs());
  return M;
}

TEST(FragmentWrapperTest, LowersStageIOAndWritesReturnMasks) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %in = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
      %dx = call float @feme.stage.derivative.x.fine.f32(float %in)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %dx, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare float @feme.stage.derivative.x.fine.f32(float)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  FragmentWrapperPass().run(*M, MAM);

  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ps_main"));
  for (const Instruction &I : instructions(*M->getFunction("ps_main")))
    if (const auto *CI = dyn_cast<CallInst>(&I))
      EXPECT_FALSE(isStageOpCall(*CI)) << *CI;

  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Regression test for roadmap H7d: `loadFragmentSystemValue()`'s `Position`
// case previously indexed the per-lane `vec4` always by `Elt.FirstComponent`
// (always 0 for a whole-builtin element), ignoring the load intrinsic's own
// per-call `Component` operand (the 3rd argument to
// `feme.stage.input.load.f32`) -- silently collapsing any read of
// `gl_FragCoord.y`/`.z`/`.w` to `.x`. This was found via a real `deqp-vk`
// reproduction of `dEQP-VK.clipping.clip_volume.depth_clamp.*`, whose own
// fragment shader reads `gl_FragCoord.z`. Verify a `Component` operand of 2
// (`.z`) actually reaches the emitted position-component GEP as index 2, not
// the buggy constant 0.
TEST(FragmentWrapperTest, ResolvesRequestedPositionComponentNotAlwaysX) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %fragz = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %fragz, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  In.SystemValue = SignatureSystemValue::Position;
  In.FirstComponent = 0;
  In.ComponentCount = 4;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(verifyModule(*M, &errs()));

  // Every lane's Position component load is a `load float, ptr %ComponentPtr`
  // where `%ComponentPtr` is the final GEP indexing the requested component
  // -- check *that* GEP's last index specifically (not any GEP in the
  // function; the outer per-lane array GEP incidentally uses a constant
  // index too, for the unrelated quad-lane selector, which would give a
  // false pass if conflated with the component-selecting GEP).
  Function *Entry = M->getFunction("ps_main");
  ASSERT_TRUE(Entry);
  bool FoundComponentTwoLoad = false;
  bool FoundComponentZeroLoad = false;
  for (const Instruction &I : instructions(*Entry)) {
    const auto *Load = dyn_cast<LoadInst>(&I);
    if (!Load || !Load->getType()->isFloatTy())
      continue;
    const auto *GEP = dyn_cast<GetElementPtrInst>(Load->getPointerOperand());
    if (!GEP)
      continue;
    auto *LastIdx =
        dyn_cast<ConstantInt>(GEP->getOperand(GEP->getNumOperands() - 1));
    if (!LastIdx)
      continue;
    if (LastIdx->getZExtValue() == 2)
      FoundComponentTwoLoad = true;
    if (LastIdx->getZExtValue() == 0)
      FoundComponentZeroLoad = true;
  }
  EXPECT_TRUE(FoundComponentTwoLoad)
      << "expected a `load float` from the requested component (2, `.z`) of "
         "the Position system value; the pre-fix bug always loaded "
         "component 0 instead";
  (void)FoundComponentZeroLoad;
}

// Regression test for roadmap H3a: gl_ViewportIndex read back as a fragment
// input (e.g. `out_color = color[gl_ViewportIndex]`) requires
// loadFragmentSystemValue() to handle SignatureSystemValue::
// ViewportArrayIndex. Verify FragmentWrapperPass lowers a system-value input
// element bound to ViewportArrayIndex without hitting the "unsupported
// fragment system value" error path.
TEST(FragmentWrapperTest, LowersViewportArrayIndexSystemValueInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %vpidx = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 %vpidx, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::SInt;
  In.SystemValue = SignatureSystemValue::ViewportArrayIndex;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::SInt;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  // Prior to the H3a fix, lowering a ViewportArrayIndex-bound input element
  // hit loadFragmentSystemValue()'s `default:` case, which calls
  // LLVMContext::emitError() -- rather than crash-testing that error path
  // directly, install a diagnostic handler and assert it is never invoked.
  bool SawError = false;
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        (void)DI;
        *reinterpret_cast<bool *>(Ctx) = true;
      },
      &SawError);

  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(SawError) << "loadFragmentSystemValue() reported an "
                             "\"unsupported fragment system value\" error "
                             "for SignatureSystemValue::ViewportArrayIndex";
  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ps_main"));
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Regression test for roadmap H73: gl_Layer read back as a fragment input
// (e.g. `dEQP-VK.mesh_shader.ext.builtin.layer`'s own
// `outColor = colors[gl_Layer]`) requires loadFragmentSystemValue() to
// handle SignatureSystemValue::RenderTargetArrayIndex, the exact same shape
// roadmap H3a already fixed for SignatureSystemValue::ViewportArrayIndex
// immediately above -- verify it is lowered the same way, without hitting
// the "unsupported fragment system value" error path.
TEST(FragmentWrapperTest, LowersRenderTargetArrayIndexSystemValueInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %layer = call i32 @feme.stage.input.load.i32(i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.i32(i32 1, i32 0, i32 0, i32 %layer, i32 0)
      ret void
    }
    declare i32 @feme.stage.input.load.i32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.i32(i32, i32, i32, i32, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::SInt;
  In.SystemValue = SignatureSystemValue::RenderTargetArrayIndex;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::SInt;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  // Prior to the H73 fix, lowering a RenderTargetArrayIndex-bound input
  // element hit loadFragmentSystemValue()'s `default:` case, which calls
  // LLVMContext::emitError() -- rather than crash-testing that error path
  // directly, install a diagnostic handler and assert it is never invoked.
  bool SawError = false;
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        (void)DI;
        *reinterpret_cast<bool *>(Ctx) = true;
      },
      &SawError);

  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(SawError) << "loadFragmentSystemValue() reported an "
                             "\"unsupported fragment system value\" error "
                             "for SignatureSystemValue::RenderTargetArrayIndex";
  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ps_main"));
  EXPECT_FALSE(verifyModule(*M, &errs()));
}

// Regression test for roadmap L114: `gl_SamplePosition` read back as a
// fragment input requires `loadFragmentSystemValue()` to handle
// `SignatureSystemValue::SamplePosition`, the same shape H3a/H73 already
// fixed for `ViewportArrayIndex`/`RenderTargetArrayIndex` above -- verify
// it is lowered the same way, without hitting the "unsupported fragment
// system value" error path, and that the requested component (`.y`, not
// just the default `.x`) is actually the one read, mirroring
// `ResolvesRequestedPositionComponentNotAlwaysX` above for `Position`'s own
// multi-component GEP resolution.
TEST(FragmentWrapperTest, LowersSamplePositionSystemValueInput) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %sposy = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %sposy, i32 0)
      ret void
    }
    declare float @feme.stage.input.load.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  In.SystemValue = SignatureSystemValue::SamplePosition;
  In.FirstComponent = 0;
  In.ComponentCount = 2;
  SignatureElement Out;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Out.ComponentType = SignatureComponentType::Float;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);

  bool SawError = false;
  Ctx.setDiagnosticHandlerCallBack(
      [](const DiagnosticInfo *DI, void *Ctx) {
        (void)DI;
        *reinterpret_cast<bool *>(Ctx) = true;
      },
      &SawError);

  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(SawError) << "loadFragmentSystemValue() reported an "
                             "\"unsupported fragment system value\" error "
                             "for SignatureSystemValue::SamplePosition";
  EXPECT_TRUE(M->getFunction("feme_cpu_entry_ps_main"));
  EXPECT_FALSE(verifyModule(*M, &errs()));

  // The requested component (`.y`, index 1) is the one actually loaded,
  // not always the default `.x` -- same check
  // `ResolvesRequestedPositionComponentNotAlwaysX` performs for `Position`.
  Function *Entry = M->getFunction("ps_main");
  ASSERT_TRUE(Entry);
  bool FoundComponentOneLoad = false;
  for (const Instruction &I : instructions(*Entry)) {
    const auto *Load = dyn_cast<LoadInst>(&I);
    if (!Load || !Load->getType()->isFloatTy())
      continue;
    const auto *GEP = dyn_cast<GetElementPtrInst>(Load->getPointerOperand());
    if (!GEP)
      continue;
    auto *LastIdx =
        dyn_cast<ConstantInt>(GEP->getOperand(GEP->getNumOperands() - 1));
    if (LastIdx && LastIdx->getZExtValue() == 1)
      FoundComponentOneLoad = true;
  }
  EXPECT_TRUE(FoundComponentOneLoad)
      << "expected a `load float` from the requested component (1, `.y`) of "
         "the SamplePosition system value";
}

// Regression test for roadmap L115(b): `lowerFragmentInterpolateAt()`'s own
// per-vertex raw-varying reload previously reused `FEnv.InputLayout` (the
// per-invocation-count-4 layout table built for `FSInput`/ordinary
// `feme.stage.input.load`) to address `FEnv.VertexInputs` (a *separate*
// per-invocation-count-3 storage block built from `FSVertexInputs` for pull-
// model interpolation's own raw per-vertex data). Since
// `feme::graphics::buildStageStorage()`'s own `ComponentStride`/`RowStride`
// fields are baked directly from its `InvocationCount` parameter, two
// storage blocks with different invocation counts for the very same
// signature are structurally incompatible for cross-addressing, even though
// they share `ElementID`s -- found via a real `deqp-vk` reproduction of
// `dEQP-VK.draw.renderpass.linear_interpolation.*`, whose green/blue/alpha
// channels (any component other than 0) came out wrong while red (component
// 0, where the wrong stride's multiplier is a harmless no-op) always
// matched. Verify the vertex-input reload's layout-table address computation
// is rooted at the dedicated `stage_fragment_vertex_input_layout` parameter,
// not `stage_input_layout`.
TEST(FragmentWrapperTest, InterpolateAtAddressesVertexInputsWithOwnLayout) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %v = call float @feme.stage.interpolate.at.offset.f32(i32 0, i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %v, i32 0)
      ret void
    }
    declare float @feme.stage.interpolate.at.offset.f32(i32, i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  In.FirstComponent = 0;
  In.ComponentCount = 1;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(verifyModule(*M, &errs()));
  // `appendFragmentStageParams()`'s new parameter lands on the per-lane
  // "wave body" function (the one `lowerFragmentInterpolateAt()` itself
  // rewrites), not on `feme_cpu_entry_ps_main` (the ABI-struct-taking outer
  // wrapper, which instead loads it from the struct and forwards it by
  // name via `CallArgs`) -- find whichever function actually declares the
  // parameter rather than assuming a fixed name.
  Function *Entry = nullptr;
  for (Function &F : *M) {
    for (Argument &Arg : F.args()) {
      if (Arg.getName() == "stage_fragment_vertex_input_layout") {
        Entry = &F;
        break;
      }
    }
    if (Entry)
      break;
  }
  ASSERT_TRUE(Entry) << "no function in the module declared a "
                        "stage_fragment_vertex_input_layout parameter";

  // The entry point must have gained a dedicated `stage_fragment_vertex_
  // input_layout` parameter, distinct from `stage_input_layout`.
  Argument *VertexInputLayoutArg = nullptr;
  Argument *InputLayoutArg = nullptr;
  for (Argument &Arg : Entry->args()) {
    if (Arg.getName() == "stage_fragment_vertex_input_layout")
      VertexInputLayoutArg = &Arg;
    else if (Arg.getName() == "stage_input_layout")
      InputLayoutArg = &Arg;
  }
  ASSERT_TRUE(VertexInputLayoutArg);
  ASSERT_TRUE(InputLayoutArg);
  EXPECT_NE(VertexInputLayoutArg, InputLayoutArg);

  // Every layout-table lookup GEP reachable from `%v`'s own worker-function
  // callee must ultimately be rooted at `VertexInputLayoutArg`, never
  // `InputLayoutArg` -- walk the (small) def-use graph from
  // `VertexInputLayoutArg`/`InputLayoutArg` themselves instead of the call
  // site, since `lowerFragmentInterpolateAt()` lowers into a per-lane loop
  // body that may live in a separate helper function after `SIMDize`/
  // `WaveLowering`.
  bool VertexInputLayoutUsed = false;
  for (const Use &U : VertexInputLayoutArg->uses()) {
    if (isa<BitCastInst>(U.getUser()) || isa<GetElementPtrInst>(U.getUser()) ||
        isa<CallInst>(U.getUser())) {
      VertexInputLayoutUsed = true;
      break;
    }
  }
  EXPECT_TRUE(VertexInputLayoutUsed)
      << "expected the new stage_fragment_vertex_input_layout parameter to "
         "actually be consumed (bitcast/GEP'd/forwarded) somewhere in the "
         "module, not left dead";

  // The regression itself: this test's shader has no ordinary
  // `feme.stage.input.load` call, only `interpolate.at.offset` -- so under
  // the bug (`FEnv.InputLayout` reused to address `FEnv.VertexInputs`),
  // `InputLayoutArg` would *also* end up dereferenced (bitcast/GEP'd) even
  // though nothing in this shader legitimately needs it. Under the fix, it
  // must stay entirely unused.
  bool InputLayoutDereferenced = false;
  for (const Use &U : InputLayoutArg->uses())
    if (isa<BitCastInst>(U.getUser()) || isa<GetElementPtrInst>(U.getUser()))
      InputLayoutDereferenced = true;
  EXPECT_FALSE(InputLayoutDereferenced)
      << "stage_input_layout was dereferenced even though this shader has "
         "no ordinary feme.stage.input.load call -- regression for roadmap "
         "L115(b)'s VertexInputs/InputLayout stride-mismatch bug: "
         "lowerFragmentInterpolateAt() must address FEnv.VertexInputs with "
         "FEnv.VertexInputLayout, not FEnv.InputLayout";
}

// Roadmap `ENV6(b)(1)` piece (d): `lowerFragmentSubpassLoad()` now consumes
// a *resolved* indexed subpass read's own new 4th operand
// (`SPIRVResourceLowering.cpp`'s `getOrInsertResolvedSubpassLoadFunc`,
// pieces (a)-(c)) by first asking `feme.cpu.image.is_mapped.i1`
// (`ImageCalls.h`) whether this attachment index's own `subpass_input_heap`
// slot was actually populated by `feme::vulkan::buildSubpassInputHeap`
// (CommandBuffer.cpp) -- it is left unmapped whenever the draw's own
// `dynamicRenderingLocalRead`/subpass-input-attachment mapping maps this
// index to `VK_ATTACHMENT_UNUSED`. This test only checks the *shape* of the
// emitted IR (the `is_mapped` query feeds a `select` between a
// `subpass_input_heap` read and a regular `image_heap` read at the
// resolved index) -- the actual runtime fallback behavior is covered by
// `ImageCallsTest.cpp`'s own `IsMapped`/`Load2D` interpreter tests.
TEST(FragmentWrapperTest,
     LowersResolvedSubpassLoadWithIsMappedGatedImageHeapFallback) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main(ptr %resource_heap, i32 %resource_heap_count,
                        ptr %sampler_heap, i32 %sampler_heap_count,
                        ptr %root_constants, i32 %root_constant_size,
                        ptr %image_heap, i32 %image_heap_count,
                        ptr %subpass_input_heap, i32 %subpass_input_heap_count) #0 {
      %v = call float @feme.stage.subpass.load.resolved.f32(
          i32 0, i32 0, i32 0, i32 2)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %v, i32 0)
      ret void
    }
    declare float @feme.stage.subpass.load.resolved.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(verifyModule(*M, &errs()));

  // `lowerFragmentSubpassLoad()` rewrites the per-lane "wave body" function
  // (the one still carrying the original `subpass_input_heap`/`image_heap`
  // parameters forwarded through Linearize/SIMDize/WaveLowering), not
  // `feme_cpu_entry_ps_main` itself -- find whichever function actually
  // declares both parameters.
  Function *Body = nullptr;
  Argument *SubpassInputHeapArg = nullptr;
  Argument *ImageHeapArg = nullptr;
  for (Function &F : *M) {
    Argument *FoundSubpass = nullptr;
    Argument *FoundImage = nullptr;
    for (Argument &Arg : F.args()) {
      if (Arg.getName() == "subpass_input_heap")
        FoundSubpass = &Arg;
      else if (Arg.getName() == "image_heap")
        FoundImage = &Arg;
    }
    if (FoundSubpass && FoundImage) {
      Body = &F;
      SubpassInputHeapArg = FoundSubpass;
      ImageHeapArg = FoundImage;
      break;
    }
  }
  ASSERT_TRUE(Body) << "no function declared both subpass_input_heap and "
                        "image_heap parameters";

  CallInst *IsMappedCall = nullptr;
  for (Instruction &I : instructions(Body)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (CI && CI->getCalledFunction() &&
        CI->getCalledFunction()->getName() == "feme.cpu.image.is_mapped.i1") {
      IsMappedCall = CI;
      break;
    }
  }
  ASSERT_TRUE(IsMappedCall) << "expected a feme.cpu.image.is_mapped.i1 "
                              "query gating the resolved subpass read";
  EXPECT_EQ(IsMappedCall->getArgOperand(0), SubpassInputHeapArg);

  // The query's own `i1` result is used directly (not wrapped in any
  // `and`/`not`) as the condition of a `select` whose two operands are
  // each one `feme.cpu.image.load.2d.v4f32` call -- one reading
  // `subpass_input_heap`, the other reading `image_heap` at the resolved
  // index (constant 2 above). Each load's own `Mask` operand is
  // separately gated by `Active` combined with the `is_mapped` result (or
  // its negation), checked only loosely here (non-null) since this test's
  // focus is the heap/index the `select` itself picks between.
  // The query's own `i1` result is combined (via `and`) with a bounds
  // check excluding `buildSubpassInputHeap`'s own two reserved
  // direct-depth/-stencil tail slots (CommandBuffer.cpp) before gating
  // the `select` between two `feme.cpu.image.load.2d.v4f32` calls -- one
  // reading `subpass_input_heap`, the other reading `image_heap` at the
  // resolved index (constant 2 above). Each load's own `Mask` operand is
  // separately gated by `Active` combined with the combined condition (or
  // its negation), checked only loosely here (non-null) since this test's
  // focus is the heap/index the `select` itself picks between.
  bool FoundGatedSelect = false;
  for (User *U : IsMappedCall->users()) {
    auto *And = dyn_cast<Instruction>(U);
    if (!And || And->getOpcode() != Instruction::And)
      continue;
    for (User *AndUser : And->users()) {
      auto *Sel = dyn_cast<SelectInst>(AndUser);
      if (!Sel || Sel->getCondition() != And)
        continue;
      auto *TrueLoad = dyn_cast<CallInst>(Sel->getTrueValue());
      auto *FalseLoad = dyn_cast<CallInst>(Sel->getFalseValue());
      if (!TrueLoad || !FalseLoad)
        continue;
      if (TrueLoad->getCalledFunction()->getName() !=
              "feme.cpu.image.load.2d.v4f32" ||
          FalseLoad->getCalledFunction()->getName() !=
              "feme.cpu.image.load.2d.v4f32")
        continue;
      if (TrueLoad->getArgOperand(0) == SubpassInputHeapArg &&
          FalseLoad->getArgOperand(0) == ImageHeapArg) {
        auto *FallbackIndex =
            dyn_cast<ConstantInt>(FalseLoad->getArgOperand(2));
        if (FallbackIndex && FallbackIndex->getZExtValue() == 2)
          FoundGatedSelect = true;
      }
    }
  }
  EXPECT_TRUE(FoundGatedSelect)
      << "expected a select, gated by the is_mapped query anded with a "
         "reserved-tail-slot bounds check, between a subpass_input_heap "
         "read and an image_heap read at the resolved index (2)";
}

// Roadmap ENV6(b)(1): `buildSubpassInputHeap` (CommandBuffer.cpp) always
// appends its own two direct-depth/-stencil placeholder slots at the tail
// of `subpass_input_heap`, at `subpass_input_heap_count - 2`/`- 1` --
// indices that collapse down to 0/1 whenever this draw has no indexed
// input-attachment mapping at all. A resolved indexed subpass read whose
// own `attachment_index` happens to equal one of those two reserved tail
// positions must still treat the slot as unmapped (fall back to
// `image_heap`) rather than false-positive-matching the unrelated direct
// depth/stencil placeholder's own data -- this is the exact collision the
// real `emulate_msrtss` CTS cluster exercises (its `i_color2`'s own
// `attachment_index == 0`). The IR shape alone can't express "this
// particular draw's heap happens to put the reserved tail at index 0" (
// that's a runtime fact), so this test simply confirms the generated
// `and` combining `is_mapped` with the `icmp ult` bounds check -- rather
// than `is_mapped` alone -- is present for every resolved indexed read,
// independent of the resolved index's own value.
TEST(FragmentWrapperTest,
     GatesResolvedSubpassLoadIsMappedByReservedTailSlotBoundsCheck) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main(ptr %resource_heap, i32 %resource_heap_count,
                        ptr %sampler_heap, i32 %sampler_heap_count,
                        ptr %root_constants, i32 %root_constant_size,
                        ptr %image_heap, i32 %image_heap_count,
                        ptr %subpass_input_heap, i32 %subpass_input_heap_count) #0 {
      %v = call float @feme.stage.subpass.load.resolved.f32(
          i32 0, i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %v, i32 0)
      ret void
    }
    declare float @feme.stage.subpass.load.resolved.f32(i32, i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(verifyModule(*M, &errs()));

  Function *Body = nullptr;
  Argument *SubpassInputHeapCountArg = nullptr;
  for (Function &F : *M) {
    Argument *FoundSubpass = nullptr;
    Argument *FoundCount = nullptr;
    for (Argument &Arg : F.args()) {
      if (Arg.getName() == "subpass_input_heap")
        FoundSubpass = &Arg;
      else if (Arg.getName() == "subpass_input_heap_count")
        FoundCount = &Arg;
    }
    if (FoundSubpass && FoundCount) {
      Body = &F;
      SubpassInputHeapCountArg = FoundCount;
      break;
    }
  }
  ASSERT_TRUE(Body) << "no function declared both subpass_input_heap and "
                        "subpass_input_heap_count parameters";

  CallInst *IsMappedCall = nullptr;
  for (Instruction &I : instructions(Body)) {
    auto *CI = dyn_cast<CallInst>(&I);
    if (CI && CI->getCalledFunction() &&
        CI->getCalledFunction()->getName() == "feme.cpu.image.is_mapped.i1") {
      IsMappedCall = CI;
      break;
    }
  }
  ASSERT_TRUE(IsMappedCall) << "expected a feme.cpu.image.is_mapped.i1 "
                              "query gating the resolved subpass read";

  // Find the `icmp ult (attachment_index, sub(subpass_input_heap_count,
  // 2))` bounds check and confirm it's `and`-ed with `IsMappedCall`.
  bool FoundBoundsCheckedAnd = false;
  for (User *U : IsMappedCall->users()) {
    auto *And = dyn_cast<Instruction>(U);
    if (!And || And->getOpcode() != Instruction::And)
      continue;
    Value *Other = And->getOperand(0) == IsMappedCall ? And->getOperand(1)
                                                       : And->getOperand(0);
    auto *Cmp = dyn_cast<ICmpInst>(Other);
    if (!Cmp || Cmp->getPredicate() != ICmpInst::ICMP_ULT)
      continue;
    auto *Sub = dyn_cast<Instruction>(Cmp->getOperand(1));
    if (!Sub || Sub->getOpcode() != Instruction::Sub)
      continue;
    if (Sub->getOperand(0) == SubpassInputHeapCountArg) {
      auto *TwoConst = dyn_cast<ConstantInt>(Sub->getOperand(1));
      if (TwoConst && TwoConst->getZExtValue() == 2)
        FoundBoundsCheckedAnd = true;
    }
  }
  EXPECT_TRUE(FoundBoundsCheckedAnd)
      << "expected is_mapped anded with (attachment_index u< "
         "subpass_input_heap_count - 2), guarding against the reserved "
         "direct-depth/-stencil tail-slot collision";
}

// Roadmap `ENV6(b)(2)(a)`: `lowerFragmentInterpolateAt()`'s `AtCentroid`
// case previously always used the plain pixel center as the centroid
// location -- a known-wrong simplification for partial-coverage (edge)
// pixels, where the pixel center itself can fall outside the primitive
// even though every actually-covered sample is inside (the real
// `dEQP-VK.pipeline.multisample_interpolation.centroid_qualifier_inside_
// primitive` cluster's failure mode). Fixed by computing a true
// coverage-weighted centroid: average `stage_fragment_sample_positions`'s
// own per-sample offsets over every sample this lane's own `Coverage`
// bitmask marks covered, falling back to the pixel center only when
// `Coverage` is entirely zero. This test only checks the *shape* of the
// emitted IR -- that the new coverage-counting/averaging code is actually
// present -- not the exact numeric result, matching this file's own
// `LowersResolvedSubpassLoadWithIsMappedGatedImageHeapFallback` precedent
// for a lowering-phase-only regression test; the runtime-phase behavior
// (`Executor.cpp`'s own padding of `SamplePositions` to 8 entries so this
// code can safely read every slot) is covered separately by
// `ExecutorTest.cpp`.
TEST(FragmentWrapperTest,
     InterpolateAtCentroidUsesCoverageWeightedAverageNotJustPixelCenter) {
  LLVMContext Ctx;
  std::unique_ptr<Module> M = parseIR(Ctx, R"(
    define void @ps_main() #0 {
      %v = call float @feme.stage.interpolate.at.centroid.f32(i32 0, i32 0, i32 0)
      call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %v, i32 0)
      ret void
    }
    declare float @feme.stage.interpolate.at.centroid.f32(i32, i32, i32)
    declare void @feme.stage.output.store.f32(i32, i32, i32, float, i32)
    attributes #0 = { "feme.shader.stage"="fragment" "feme.cpu.wavesize"="4" }
  )");
  ASSERT_TRUE(M);

  EntrySignature Sig;
  SignatureElement In;
  In.ElementID = 0;
  In.Direction = SignatureDirection::Input;
  In.ComponentType = SignatureComponentType::Float;
  In.FirstComponent = 0;
  In.ComponentCount = 1;
  SignatureElement Out = In;
  Out.ElementID = 1;
  Out.Direction = SignatureDirection::Output;
  Sig.Elements = {In, Out};
  dxil::setEntrySignature(*M->getFunction("ps_main"), Sig);

  ModuleAnalysisManager MAM;
  LinearizePass().run(*M, MAM);
  SIMDizePass(4).run(*M, MAM);
  WaveLoweringPass().run(*M, MAM);
  FragmentWrapperPass().run(*M, MAM);

  EXPECT_FALSE(verifyModule(*M, &errs()));

  // `lowerFragmentInterpolateAt()` rewrites the per-lane "wave body"
  // function (the one still carrying the original
  // `stage_fragment_sample_positions` parameter forwarded through
  // Linearize/SIMDize/WaveLowering), not `feme_cpu_entry_ps_main` itself.
  Function *Body = nullptr;
  Argument *SamplePositionsArg = nullptr;
  for (Function &F : *M) {
    for (Argument &Arg : F.args()) {
      if (Arg.getName() == "stage_fragment_sample_positions") {
        Body = &F;
        SamplePositionsArg = &Arg;
        break;
      }
    }
    if (Body)
      break;
  }
  ASSERT_TRUE(Body) << "no function declared a "
                        "stage_fragment_sample_positions parameter";

  // The fix reads all 8 of the (now unconditionally safe,
  // `Executor.cpp`-padded) sample-position table slots: find every GEP
  // directly rooted at `SamplePositionsArg` (opaque pointers mean
  // `lowerFragmentInterpolateAt()`'s own `CreateBitCast` to the same
  // pointer type is a no-op, not a real instruction), and collect each
  // one's own constant sample index (the GEP indexing directly into the
  // `[N x [2 x float]]` table, not the inner `{x, y}` component GEP
  // chained off of it).
  llvm::SmallSet<uint64_t, 8> SampleIndices;
  for (const Use &U : SamplePositionsArg->uses()) {
    auto *GEP = dyn_cast<GetElementPtrInst>(U.getUser());
    if (!GEP || GEP->getNumIndices() != 1)
      continue;
    if (auto *Idx = dyn_cast<ConstantInt>(GEP->getOperand(1)))
      SampleIndices.insert(Idx->getZExtValue());
  }
  EXPECT_EQ(SampleIndices.size(), 8u)
      << "expected lowerFragmentInterpolateAt()'s AtCentroid case to read "
         "all 8 sample-position table slots (indices 0-7), not just reuse "
         "the plain pixel center";

  // The averaging itself: a `uitofp` of the covered-sample popcount is new
  // and unique to this fix -- nothing else in this lowering ever converts
  // an integer count to float.
  bool FoundCoverageCountToFloat = false;
  for (Instruction &I : instructions(Body))
    if (isa<UIToFPInst>(&I))
      FoundCoverageCountToFloat = true;
  EXPECT_TRUE(FoundCoverageCountToFloat)
      << "expected a uitofp of the covered-sample count, feeding the "
         "coverage-weighted average's own fdiv";

  // The pixel-center fallback (for the all-zero-coverage case) must still
  // be reachable via a `select` gated on whether any sample was covered
  // at all (an `icmp ne ..., 0`), distinct from the pre-existing
  // active-lane-masking `select` at the very end of this lowering (gated
  // on an `extractelement`, not an `icmp`).
  bool FoundCoverageGatedSelect = false;
  for (Instruction &I : instructions(Body)) {
    auto *Sel = dyn_cast<SelectInst>(&I);
    if (!Sel)
      continue;
    auto *Cmp = dyn_cast<ICmpInst>(Sel->getCondition());
    if (Cmp && Cmp->getPredicate() == ICmpInst::ICMP_NE)
      FoundCoverageGatedSelect = true;
  }
  EXPECT_TRUE(FoundCoverageGatedSelect)
      << "expected a select, gated on whether the covered-sample count is "
         "nonzero, falling back to the plain pixel center";
}

} // namespace
