//===- ImageSamplingTest.cpp - Tests for libFeMeRuntimeCPU image helpers -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These tests JIT-compile the actual `libFeMeRuntimeCPU` bitcode (see
// feme/runtime/CPU/FeMeRuntimeCPU.c) and call its canonical
// `feme.cpu.image.*` helpers directly against real, host-allocated image
// and sampler heaps laid out exactly as `feme::cpu::FemeImageDescriptor`/
// `FemeSamplerDescriptor` (feme/include/feme/Target/CPU/RuntimeABI.h)
// describe -- the same JIT-and-call-directly strategy
// unittests/Runtime/CPU/RuntimeCPUTest.cpp already uses for the buffer
// helpers, exercising the actual addressing/filtering/format-conversion
// logic rather than just that the IR parses.
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/RuntimeABI.h"
#include "feme/Target/CPU/RuntimeCPU.h"

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <cstring>
#include <limits>

using namespace feme::cpu;
using namespace llvm;

namespace {

/// See RuntimeCPUTest.cpp's identical helper for why both spellings are
/// tried.
Function *getRuntimeFunction(Module &M, StringRef Name) {
  if (Function *F = M.getFunction(Name))
    return F;
  return M.getFunction(("\1" + Name).str());
}

/// A host-side re-implementation of `femeRTFastLog2`'s exact approximation
/// formula (FeMeRuntimeCPU.c), used only by the `QueryLod2D*` tests below
/// to compute an expected raw/unclamped lod value against a tight
/// tolerance (roadmap L66(h) tightened this approximation's own max
/// mid-octave error from ~0.057 to ~0.0013 log2 units -- see that
/// function's own doc for why -- and, unlike the single-term linear
/// formula it replaced, this one genuinely is exact at every power of
/// two, e.g. `femeRTFastLog2(1.0) == 0.0` exactly, not just approximately).
float expectedFastLog2(float X) {
  uint32_t Bits;
  memcpy(&Bits, &X, sizeof(Bits));
  int32_t Exp = (int32_t)(Bits >> 23) - 127;
  uint32_t MantissaBits = (Bits & 0x007FFFFFu) | (127u << 23);
  float M;
  memcpy(&M, &MantissaBits, sizeof(M));
  float F = M - 1.0f;
  float Poly = F * (1.42349512f + F * (-0.58777299f + F * 0.16559316f));
  return (float)Exp + Poly;
}

class ImageSamplingTest : public testing::Test {
protected:
  static void SetUpTestSuite() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
  }

  LLVMContext Ctx;
  Module *M = nullptr; // Owned by Engine.
  std::unique_ptr<ExecutionEngine> Engine;

  void SetUp() override {
    Expected<std::unique_ptr<Module>> ModOrErr =
        parseBitcodeFile(getRuntimeCPUBitcode(), Ctx);
    ASSERT_THAT_EXPECTED(ModOrErr, Succeeded());
    auto Owner = std::move(*ModOrErr);
    M = Owner.get();
    std::string Error;
    Engine.reset(EngineBuilder(std::move(Owner)).setErrorStr(&Error).create());
    ASSERT_TRUE(Engine) << Error;
  }

  /// Adds a `void @<Name>(<Callee's own params>..., ptr out)` wrapper
  /// forwarding every argument to \p Callee and storing its result through
  /// the trailing `out` pointer -- generic over \p Callee's exact
  /// signature (unlike RuntimeCPUTest.cpp's fixed-shape buffer wrappers),
  /// since `feme.cpu.image.*` calls don't all share one operand layout.
  Function *addWrapper(StringRef Name, StringRef Callee) {
    Function *Target = getRuntimeFunction(*M, Callee);
    assert(Target && "runtime function not found in libFeMeRuntimeCPU bitcode");
    LLVMContext &C = M->getContext();
    Type *PtrTy = PointerType::get(C, 0);
    SmallVector<Type *, 16> ParamTypes(Target->getFunctionType()->params());
    ParamTypes.push_back(PtrTy);
    FunctionType *WrapperTy =
        FunctionType::get(Type::getVoidTy(C), ParamTypes, false);
    Function *Wrapper =
        Function::Create(WrapperTy, Function::ExternalLinkage, Name, M);
    BasicBlock *BB = BasicBlock::Create(C, "entry", Wrapper);
    IRBuilder<> Builder(BB);
    SmallVector<Value *, 16> Args;
    for (Argument &Arg : Wrapper->args())
      Args.push_back(&Arg);
    Value *Out = Args.pop_back_val();
    Value *Result = Builder.CreateCall(Target, Args);
    Builder.CreateStore(Result, Out);
    Builder.CreateRetVoid();
    return Wrapper;
  }

  template <typename FnTy> FnTy resolve(Function *F) {
    uint64_t Addr = Engine->getFunctionAddress(F->getName().str());
    // A null address here means MCJIT failed to resolve/finalize this
    // symbol; calling through it crashes with an uninformative `pc=0x0`
    // segfault and no diagnostic at all (this is exactly what a batch of
    // reported UBSan `StoreWritesTexelInto*` failures looked like on
    // another host/toolchain, and which did not reproduce here -- this
    // assert turns any recurrence into an immediate, named failure
    // instead of a bare crash).
    assert(Addr && "MCJIT failed to resolve a runtime function address");
    return reinterpret_cast<FnTy>(Addr);
  }

  /// Resolves \p Name directly, with no `addWrapper` IR wrapper -- for
  /// `feme.cpu.image.store.2d.*` (roadmap H19a), whose `Texel` parameter is
  /// already a real, natural-width vector matching the C ABI exactly (see
  /// `StoreFn`/`StoreI32Fn`'s comment), so there is no return value to
  /// funnel through an `out` pointer and nothing a wrapper would add.
  template <typename FnTy> FnTy resolveRuntime(StringRef Name) {
    Function *F = getRuntimeFunction(*M, Name);
    assert(F && "runtime function not found in libFeMeRuntimeCPU bitcode");
    return resolve<FnTy>(F);
  }
};

/// `feme.cpu.image.sample.2d.v4f32`'s own operand shape: image/sampler
/// heaps and descriptor indices, `(U, V)`, the four screen-space partial
/// derivatives of `(U, V)` an implicit-LOD sample's real mip/anisotropy
/// selection consults (roadmap H7i; ignored, but still present, for an
/// explicit-LOD sample), `Lod`/`UseExplicitLod`, a float `Bias` (roadmap
/// L58), the integer `(OffsetX,
/// OffsetY)` texel offset and float `MinLodClamp` floor roadmap L26 added,
/// an active-lane mask, and the `<4 x float>` result written through the
/// trailing `out` pointer.
using SampleFn = void (*)(const FemeImageDescriptor *, uint32_t,
                          const FemeSamplerDescriptor *, uint32_t, uint32_t,
                          uint32_t, float, float, float, float, float, float,
                          float, bool, float, int32_t, int32_t, float, bool,
                          void *);
/// `feme.cpu.image.samplecmp.2d.f32`'s own operand shape: mirroring
/// `SampleFn`'s `(image_heap..v)` prefix, plus a real `DUdX`/`DUdY`/
/// `DVdX`/`DVdY` screen-space partial-derivative quartet (roadmap L66(c);
/// mirroring `SampleFn`'s own identically-named parameters -- ignored,
/// but still present, for an explicit-LOD `samplecmplevelzero` call),
/// `Lod`/`UseExplicitLod`, `Dref`, a float `Bias` (roadmap L52(b)), the
/// integer `(OffsetX, OffsetY)` texel offset (roadmap L50d) and float
/// `MinLodClamp` floor (roadmap L52(c)), an active-lane mask, and the
/// `float` result written through the trailing `out` pointer.
using SampleCmpFn = void (*)(const FemeImageDescriptor *, uint32_t,
                             const FemeSamplerDescriptor *, uint32_t, uint32_t,
                             uint32_t, float, float, float, float, float, float,
                             float, bool, float, float, int32_t, int32_t, float,
                             bool, void *);
/// `feme.cpu.image.gathercmp.2d.v4f32`'s own operand shape: mirroring
/// `SampleCmpFn`'s `(image_heap..v)` prefix, but with none of `SampleFn`/
/// `SampleCmpFn`'s own `DUdX`/`DUdY`/`DVdX`/`DVdY`/`Lod`/`UseExplicitLod`/
/// `Bias`/`MinLodClamp` operands at all -- `OpImageDrefGather` has no
/// `Lod`/`Bias`/`Grad`/`MinLod` image operand in the SPIR-V spec (a
/// gather always operates at mip level 0), so this shape's own operand
/// list (roadmap L7d) is `(U, V, Dref, OffsetX, OffsetY, Mask, out)`, a
/// `<4 x float>` result (one comparison result per gathered texel)
/// written through the trailing `out` pointer rather than `SampleCmpFn`'s
/// own single `float`.
using GatherCmpFn = void (*)(const FemeImageDescriptor *, uint32_t,
                             const FemeSamplerDescriptor *, uint32_t, uint32_t,
                             uint32_t, float, float, float, int32_t, int32_t,
                             bool, void *);
/// `feme.cpu.image.gather.2d.v4f32`'s own operand shape (roadmap L7g):
/// identical to `GatherCmpFn` above, except the `Dref` (`float`) position
/// holds `Component` (`int32_t`, 0-3 selecting R/G/B/A) instead.
using GatherFn = void (*)(const FemeImageDescriptor *, uint32_t,
                          const FemeSamplerDescriptor *, uint32_t, uint32_t,
                          uint32_t, float, float, int32_t, int32_t, int32_t,
                          bool, void *);
/// `feme.cpu.image.gathercmp.array2d.v4f32`'s own operand shape (roadmap
/// H124q): the `Array2D` counterpart of `GatherCmpFn` above, adding an
/// `ArrayLayer` (`float`) operand right after `V`.
using GatherCmpArray2DFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                    const FemeSamplerDescriptor *, uint32_t,
                                    uint32_t, uint32_t, float, float, float,
                                    float, int32_t, int32_t, bool, void *);
/// `feme.cpu.image.gather.array2d.v4f32`'s own operand shape (roadmap
/// H124q): the `Array2D` counterpart of `GatherFn` above, adding an
/// `ArrayLayer` (`float`) operand right after `V`.
using GatherArray2DFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                 const FemeSamplerDescriptor *, uint32_t,
                                 uint32_t, uint32_t, float, float, float,
                                 int32_t, int32_t, int32_t, bool, void *);
/// `feme.cpu.image.gathercmp.cube.v4f32`'s own operand shape (roadmap
/// H124r): the `Cube` counterpart of `GatherCmpFn` above, taking a
/// direction vector (`DirX`, `DirY`, `DirZ`) in place of `(U, V)`,
/// mirroring `SampleCubeFn`'s relationship to `SampleFn`, and with no
/// `OffsetX`/`OffsetY` operand at all (SPIR-V forbids `ConstOffset`
/// against `Dim::Cube`, and HLSL's own `TextureCube::GatherCmp()` has no
/// offset overload).
using GatherCmpCubeFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                 const FemeSamplerDescriptor *, uint32_t,
                                 uint32_t, uint32_t, float, float, float, float,
                                 bool, void *);
/// `feme.cpu.image.gather.cube.v4f32`'s own operand shape (roadmap
/// H124r): the `Cube` counterpart of `GatherFn` above, mirroring
/// `GatherCmpCubeFn`'s direction-vector coordinate and lack of an offset
/// operand.
using GatherCubeFn = void (*)(const FemeImageDescriptor *, uint32_t,
                              const FemeSamplerDescriptor *, uint32_t, uint32_t,
                              uint32_t, float, float, float, int32_t, bool,
                              void *);
/// `feme.cpu.image.gather.2d.v4i32`'s own operand shape (roadmap
/// L125(k)): identical to `GatherFn` above, except the `out` pointer is
/// `<4 x i32>`-shaped rather than `<4 x float>`, for a gather against an
/// integer-sampled (`usampler2D`/`isampler2D`) image.
using GatherI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                             const FemeSamplerDescriptor *, uint32_t, uint32_t,
                             uint32_t, float, float, int32_t, int32_t, int32_t,
                             bool, void *);
/// `feme.cpu.image.gather.array2d.v4i32`'s own operand shape (roadmap
/// L125(k)): the `Array2D` counterpart of `GatherI32Fn` above, mirroring
/// `GatherArray2DFn`'s relationship to `GatherFn`.
using GatherArray2DI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                                    const FemeSamplerDescriptor *, uint32_t,
                                    uint32_t, uint32_t, float, float, float,
                                    int32_t, int32_t, int32_t, bool, void *);
/// `feme.cpu.image.gather.cube.v4i32`'s own operand shape (roadmap
/// L125(k)): the `Cube` counterpart of `GatherI32Fn` above, mirroring
/// `GatherCubeFn`'s direction-vector coordinate and lack of an offset
/// operand.
using GatherCubeI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                                 const FemeSamplerDescriptor *, uint32_t,
                                 uint32_t, uint32_t, float, float, float,
                                 int32_t, bool, void *);
/// Roadmap L52a: the ordinary (non-comparison) `Texture1D` counterpart of
/// `SampleFn` -- a single `U` coordinate, no `ConstOffset` (mirroring
/// `SampleArrayFn`'s own simpler scope, see `ImageCallKind::Sample1D`'s
/// own doc for why). Roadmap L61(c) adds a real `Bias`/`MinLodClamp`
/// pair, mirroring `SampleFn`'s own identically-named trailing
/// parameters. Roadmap L63 adds a real `DUdX`/`DUdY` screen-space
/// partial-derivative pair right after `U`, mirroring `SampleFn`'s own
/// identically-named parameters narrowed to this shape's single
/// addressed coordinate component. Roadmap L66(d) adds a real `int32_t`
/// `ConstOffset` operand between `Bias` and `MinLodClamp` -- a bare
/// scalar rather than `SampleFn`'s own `OffsetX`/`OffsetY` pair (see
/// `isSupportedOffset`'s own comment for why this shape's offset is
/// scalar, not a vector).
using Sample1DFn = void (*)(const FemeImageDescriptor *, uint32_t,
                            const FemeSamplerDescriptor *, uint32_t, uint32_t,
                            uint32_t, float, float, float, float, bool, float,
                            int32_t, float, bool, void *);
/// Roadmap L52a: the `Texture1DArray` counterpart of `Sample1DFn`, adding
/// a float `ArrayLayer` coordinate before `Lod`, mirroring
/// `SampleArrayFn`'s relationship to `SampleFn`. Roadmap L61(c) adds the
/// same `Bias`/`MinLodClamp` pair `Sample1DFn` gained. Roadmap L63 adds
/// the same `DUdX`/`DUdY` pair `Sample1DFn` gained, right after
/// `ArrayLayer`. Roadmap L66(d) adds the same scalar `int32_t`
/// `ConstOffset` operand `Sample1DFn` gained.
using Sample1DArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                 const FemeSamplerDescriptor *, uint32_t,
                                 uint32_t, uint32_t, float, float, float, float,
                                 float, bool, float, int32_t, float, bool,
                                 void *);

/// Roadmap L66(a), extended with a real `Bias`/`MinLodClamp` pair by
/// roadmap L67(a) and a real `ConstOffset` triple by roadmap L67(c): the
/// `Texture3D` counterpart of `Sample1DFn` -- a real `(U, V, W)`
/// coordinate plus its own screen-space derivative triple (`DUdX`/`DUdY`,
/// `DVdX`/`DVdY`, `DWdX`/`DWdY`), a real `Bias`/`MinLodClamp` pair, and a
/// real `OffsetX`/`OffsetY`/`OffsetZ` triple (see `ImageCallKind::
/// Sample3D`'s own doc for why it is 3-wide, unlike `SampleFn`'s own
/// 2-wide `OffsetX`/`OffsetY`).
using Sample3DFn = void (*)(const FemeImageDescriptor *, uint32_t,
                            const FemeSamplerDescriptor *, uint32_t, uint32_t,
                            uint32_t, float, float, float, float, float, float,
                            float, float, float, float, bool, float, int32_t,
                            int32_t, int32_t, float, bool, void *);
/// Roadmap L54: the depth-comparison counterpart of `Sample1DFn`,
/// mirroring `SampleCmpFn`'s relationship to `SampleFn` -- a single `U`
/// coordinate, and a real `Bias`/`MinLodClamp` pair after `Dref` as of
/// roadmap L62. Roadmap L66(f) added a real `DUdX`/`DUdY` scalar
/// derivative pair after `U`, mirroring `Sample1DFn`'s own identically-
/// named parameters. Roadmap L66(k) added a real, bare-scalar `Offset`
/// after `Bias`, mirroring `Sample1DFn`'s own identically-placed
/// `OffsetX` parameter (see `ImageCallKind::SampleCmp1D`'s own doc).
using SampleCmp1DFn = void (*)(const FemeImageDescriptor *, uint32_t,
                               const FemeSamplerDescriptor *, uint32_t,
                               uint32_t, uint32_t, float, float, float, float,
                               bool, float, float, int32_t, float, bool,
                               void *);
/// Roadmap L54: the `Texture1DArray` counterpart of `SampleCmp1DFn`,
/// adding a float `ArrayLayer` coordinate before `Lod`, mirroring
/// `Sample1DArrayFn`'s relationship to `Sample1DFn`. Roadmap L66(f) added
/// the same `DUdX`/`DUdY` pair `SampleCmp1DFn` gained, after `ArrayLayer`.
/// Roadmap L66(k) added the same `Offset` parameter `SampleCmp1DFn`
/// gained, after `Bias`.
using SampleCmpArray1DFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                    const FemeSamplerDescriptor *, uint32_t,
                                    uint32_t, uint32_t, float, float, float,
                                    float, float, bool, float, float, int32_t,
                                    float, bool, void *);

/// Roadmap L52e: `Texture2D`'s own `OpImageQueryLod` counterpart -- no
/// `(U, V)` coordinate operand at all (see `ImageCallKind::QueryLod2D`'s
/// own doc for why), just the caller's own screen-space partial
/// derivatives and an active-lane mask; the result's own two lanes
/// (clamped level, unclamped lod) are written through the trailing `out`
/// pointer as a `float[2]`.
using QueryLod2DFn = void (*)(const FemeImageDescriptor *, uint32_t,
                              const FemeSamplerDescriptor *, uint32_t,
                              uint32_t, uint32_t, float, float, float, float,
                              bool, void *);
using LoadFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                        int32_t, int32_t, uint32_t, uint32_t, bool, void *);
/// The `feme.cpu.image.load.2d.v4i32` (roadmap E26) counterpart of `LoadFn`,
/// same operand shape but a `<4 x i32>`-shaped `out` -- also takes a
/// `Sample` operand (roadmap H19g), like `LoadFn`'s own.
using LoadI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                           int32_t, int32_t, uint32_t, uint32_t, bool, void *);
/// `feme.cpu.image.sample.2d.v4i32`'s own operand shape (roadmap H109):
/// unlike `SampleFn`, an integer-channel sample is always explicit-LOD,
/// nearest-filtered only -- no `DUdX`/`DUdY`/`DVdX`/`DVdY`/
/// `UseExplicitLod`/`Bias`/`MinLodClamp` operand at all, just `(U, V,
/// Lod, OffsetX, OffsetY, Mask)` and a `<4 x i32>`-shaped `out` (roadmap
/// L125(h) uses this to verify the in-bounds-swizzle fix's own integer
/// counterpart).
using SampleI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                             const FemeSamplerDescriptor *, uint32_t, uint32_t,
                             uint32_t, float, float, float, int32_t, int32_t,
                             bool, void *);
/// The roadmap H7b-a `Texture2DArray` counterpart of `SampleFn`, adding a
/// float `ArrayLayer` coordinate (rounded to nearest, clamped) before the
/// four screen-space partial derivatives of `(U, V)` -- also gains
/// (roadmap L60(a)) its own float `Bias`, and (roadmap L33) the same
/// integer `(OffsetX, OffsetY)` texel offset `SampleFn` documents, plus a
/// trailing float `MinLodClamp`, mirroring `SampleFn`'s own operands
/// above.
using SampleArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                               const FemeSamplerDescriptor *, uint32_t,
                               uint32_t, uint32_t, float, float, float, float,
                               float, float, float, float, bool, float, int32_t,
                               int32_t, float, bool, void *);
/// The roadmap H7b-a `Texture2DArray` counterpart of `LoadFn`, adding an
/// integer `Layer` coordinate before `Mip`.
using LoadArrayFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                             int32_t, int32_t, int32_t, uint32_t, uint32_t,
                             bool, void *);
/// The roadmap H7b-a `Texture2DArray` counterpart of `LoadI32Fn`. Widened
/// (roadmap H19m) to add the same `Sample` operand before `Mask` that
/// `LoadArrayFn` (float) already had -- see `ImageCalls.h`'s own
/// `createLoad2DArrayI32` comment.
using LoadArrayI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                                uint32_t, int32_t, int32_t, int32_t, uint32_t,
                                uint32_t, bool, void *);
/// The roadmap H7b-a `TextureCube` counterpart of `SampleFn`: a
/// direction-vector coordinate (`DirX`, `DirY`, `DirZ`) instead of `(U, V)`,
/// followed (roadmap L56) by six screen-space partial-derivative operands
/// of that direction vector (`DDirXdX`, `DDirXdY`, `DDirYdX`, `DDirYdY`,
/// `DDirZdX`, `DDirZdY`, consulted only for an implicit-LOD sample -- see
/// `femeRTComputeCubeUVDerivatives`'s doc, `FeMeRuntimeCPU.c`), a float
/// `Bias` (roadmap L58), and (roadmap L26)
/// a trailing float `MinLodClamp` floor before the mask --
/// but no integer texel offset, unlike `SampleFn` (SPIR-V forbids
/// `ConstOffset` against a cube image; see `isSupportedOffset`'s comment).
using SampleCubeFn = void (*)(const FemeImageDescriptor *, uint32_t,
                              const FemeSamplerDescriptor *, uint32_t,
                              uint32_t, uint32_t, float, float, float, float,
                              float, float, float, float, float, float, bool,
                              float, float, bool, void *);
/// The roadmap H7b-a `TextureCubeArray` counterpart of `SampleCubeFn`,
/// adding a float `ArrayLayer` coordinate (selecting a six-layer cube
/// element) before `Lod`; also gains its own roadmap L56
/// `DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/`DDirZdX`/`DDirZdY` operands and
/// (roadmap L60(a)) a float `Bias` and trailing float `MinLodClamp`,
/// mirroring `SampleCubeFn`'s own new operands above.
using SampleCubeArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                   const FemeSamplerDescriptor *, uint32_t,
                                   uint32_t, uint32_t, float, float, float,
                                   float, float, float, float, float, float,
                                   float, float, bool, float, float, bool,
                                   void *);

/// The roadmap L48 `Texture2DArray` counterpart of `SampleCmpFn`, adding
/// a float `ArrayLayer` coordinate before `Lod` -- mirroring
/// `SampleArrayFn`'s relationship to `SampleFn`. Also gains (roadmap
/// L52(c)) its own trailing float `MinLodClamp`, mirroring
/// `SampleCmpFn`'s own new operand above. Roadmap L66(g) widens it again
/// with a real `DUdX`/`DUdY`/`DVdX`/`DVdY` quartet (inserted after
/// `ArrayLayer`, before `Lod`), mirroring `SampleCmpFn`'s own identical
/// `Grad` widening -- only `U`/`V`, never `ArrayLayer`, is ever
/// differentiated.
using SampleCmpArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                  const FemeSamplerDescriptor *, uint32_t,
                                  uint32_t, uint32_t, float, float, float,
                                  float, float, float, float, float, bool,
                                  float, float, int32_t, int32_t, float, bool,
                                  void *);
/// The roadmap L48 `TextureCube` counterpart of `SampleCmpFn`: a
/// direction-vector coordinate (`DirX`, `DirY`, `DirZ`) instead of `(U,
/// V)`, mirroring `SampleCubeFn`'s relationship to `SampleFn`. Also gains
/// (roadmap L52(c)) its own trailing float `MinLodClamp`, matching
/// `SampleCmp2D`'s own new operand (unlike SPIR-V's `ConstOffset`, which
/// forbids `Dim::Cube`, `MinLod` is legal against any dimensionality).
/// Roadmap L66(h) widens it again with a real `DDirXdX`/`DDirXdY`/
/// `DDirYdX`/`DDirYdY`/`DDirZdX`/`DDirZdY` sextuple (inserted after
/// `DirZ`, before `Lod`), mirroring `SampleCubeFn`'s own identical
/// direction-vector derivative widening.
using SampleCmpCubeFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                 const FemeSamplerDescriptor *, uint32_t,
                                 uint32_t, uint32_t, float, float, float, float,
                                 float, float, float, float, float, float, bool,
                                 float, float, float, bool, void *);
/// The roadmap L48 `TextureCubeArray` counterpart of `SampleCmpCubeFn`,
/// adding a float `ArrayLayer` coordinate before `Lod`. Also gains
/// (roadmap L52(c)) its own trailing float `MinLodClamp`, mirroring
/// `SampleCmpCubeFn`'s own new operand above. Roadmap L66(i) widens it
/// again with a real `DDirXdX`/`DDirXdY`/`DDirYdX`/`DDirYdY`/`DDirZdX`/
/// `DDirZdY` sextuple (inserted after `DirZ`, before `ArrayLayer`),
/// mirroring `SampleCmpCubeFn`'s own identical direction-vector
/// derivative widening (roadmap L66(h)).
using SampleCmpCubeArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                      const FemeSamplerDescriptor *, uint32_t,
                                      uint32_t, uint32_t, float, float, float,
                                      float, float, float, float, float, float,
                                      float, float, bool, float, float, float,
                                      bool, void *);

/// A real, ABI-matching 4-lane vector type (unlike four separate scalar
/// parameters, which the x86-64 SysV convention would place in four
/// separate registers rather than one packed 128-bit one): mirrors
/// `FeMeRuntimeCPU.c`'s own `FemeRTv4f32`/an `<4 x i32>` vector exactly,
/// so `feme.cpu.image.store.2d.*`'s (roadmap H19a) `Texel` parameter can be
/// resolved and called directly, with no IR-level wrapper needed (unlike
/// `addWrapper`'s call-through-LLVM-IR strategy above, this test group's
/// store functions are resolved and called as raw, JIT-compiled machine
/// code, so the C++ call site's own argument types must already match the
/// real ABI).
typedef float FeMeTestV4F32 __attribute__((vector_size(16)));
typedef int32_t FeMeTestV4I32 __attribute__((vector_size(16)));
using StoreFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                         int32_t, int32_t, FeMeTestV4F32, bool);
using StoreI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                            int32_t, int32_t, FeMeTestV4I32, bool);
/// The roadmap H19b `Texture2DArray` counterpart of `StoreFn`, adding an
/// integer `Layer` coordinate before the texel value -- mirroring
/// `LoadArrayFn`'s relationship to `LoadFn`.
using StoreArrayFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                              int32_t, int32_t, int32_t, FeMeTestV4F32, bool);
/// The roadmap H19b integer-format counterpart of `StoreArrayFn`.
using StoreArrayI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                                 uint32_t, int32_t, int32_t, int32_t,
                                 FeMeTestV4I32, bool);
/// The roadmap H19g plain (non-arrayed) multisampled 2D counterpart of
/// `StoreFn`, adding an integer `Sample` coordinate before the texel
/// value -- the write-side counterpart of `LoadFn`'s own `Sample` operand
/// (mirroring `StoreArrayFn`'s relationship to `LoadArrayFn` above, but
/// for a per-sample index instead of an array layer).
using StoreMSFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                           int32_t, int32_t, uint32_t, FeMeTestV4F32, bool);
/// The roadmap H19g integer-format counterpart of `StoreMSFn`.
using StoreMSI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                              int32_t, int32_t, uint32_t, FeMeTestV4I32, bool);
/// The roadmap H19m arrayed-*and*-multisampled 2D counterpart of `StoreFn`,
/// combining `StoreArrayFn`'s own integer `Layer` operand and `StoreMSFn`'s
/// own integer `Sample` operand -- both before the texel value.
using StoreArrayMSFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                uint32_t, int32_t, int32_t, int32_t, uint32_t,
                                FeMeTestV4F32, bool);
/// The roadmap H19m integer-format counterpart of `StoreArrayMSFn`.
using StoreArrayMSI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                                   uint32_t, int32_t, int32_t, int32_t,
                                   uint32_t, FeMeTestV4I32, bool);
/// The roadmap H19c plain-1D counterpart of `LoadFn`: a single `X` texel
/// coordinate, no `Y`.
using Load1DFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                          int32_t, uint32_t, uint32_t, bool, void *);
/// The roadmap H19c plain-1D counterpart of `LoadI32Fn`.
using Load1DI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                             int32_t, uint32_t, bool, void *);
/// The roadmap H19c plain-1D counterpart of `StoreFn`.
using Store1DFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                           int32_t, FeMeTestV4F32, bool);
/// The roadmap H19c plain-1D counterpart of `StoreI32Fn`.
using Store1DI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                              int32_t, FeMeTestV4I32, bool);
/// The roadmap H19e arrayed-1D counterpart of `Load1DFn`, adding an
/// integer `Layer` coordinate before `Mip` -- mirroring `LoadArrayFn`'s
/// relationship to `LoadFn`.
using Load1DArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                               uint32_t, int32_t, int32_t, uint32_t, uint32_t,
                               bool, void *);
/// The roadmap H19e arrayed-1D counterpart of `Load1DI32Fn`.
using Load1DArrayI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                                  uint32_t, int32_t, int32_t, uint32_t, bool,
                                  void *);
/// The roadmap H19e arrayed-1D counterpart of `Store1DFn`.
using Store1DArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                uint32_t, int32_t, int32_t, FeMeTestV4F32,
                                bool);
/// The roadmap H19e arrayed-1D counterpart of `Store1DI32Fn`.
using Store1DArrayI32Fn = void (*)(const FemeImageDescriptor *, uint32_t,
                                   uint32_t, int32_t, int32_t, FeMeTestV4I32,
                                   bool);
/// The roadmap H19c plain-3D counterpart of `LoadFn`: an `(X, Y, Z)` texel
/// coordinate, never an array layer -- a 3D image is never arrayed.
using Load3DFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                          int32_t, int32_t, int32_t, uint32_t, uint32_t, bool,
                          void *);
/// The roadmap H19c plain-3D counterpart of `LoadI32Fn`.
using Load3DI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                             int32_t, int32_t, int32_t, uint32_t, bool,
                             void *);
/// The roadmap H19c plain-3D counterpart of `StoreFn`.
using Store3DFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                           int32_t, int32_t, int32_t, FeMeTestV4F32, bool);
/// The roadmap H19c plain-3D counterpart of `StoreI32Fn`.
using Store3DI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                              int32_t, int32_t, int32_t, FeMeTestV4I32, bool);

/// Builds a single-mip-level, single-layer 2D `FemeImageDescriptor` over
/// \p Storage (assumed row-major, tightly packed at \p Format's element
/// size), sampled and (optionally) storage-capable.
FemeImageDescriptor makeImage2D(void *Storage, uint64_t SizeInBytes,
                                uint32_t Width, uint32_t Height,
                                ResourceFormat Format,
                                FemeImageSubresourceLayout &Layout,
                                uint32_t ExtraFlags = 0) {
  Layout = {0, 0, 0, 0};
  uint64_t ElemSize = SizeInBytes / (uint64_t)Width / (uint64_t)Height;
  Layout.RowPitch = Width * ElemSize;
  Layout.SlicePitch = Layout.RowPitch * Height;

  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = SizeInBytes;
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(Format);
  Img.Width = Width;
  Img.Height = Height;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | ExtraFlags;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  return Img;
}

/// The roadmap H7b-a array counterpart of `makeImage2D` above: a
/// single-mip-level, \p ArrayLayers-layer 2D `FemeImageDescriptor` over \p
/// Storage (assumed row-major, tightly packed, layer-major -- each layer's
/// own texels contiguous before the next layer's), sampled. Used both for
/// plain `Texture2DArray` tests and (with `ArrayLayers` a multiple of 6)
/// `TextureCube`/`TextureCubeArray` tests, since a cube(array) is purely a
/// view-level addressing convention over an ordinary 2D-array-shaped image
/// (see FeMeVulkanDesign.md's H7b update).
FemeImageDescriptor makeImage2DArray(void *Storage, uint64_t SizeInBytes,
                                     uint32_t Width, uint32_t Height,
                                     uint32_t ArrayLayers,
                                     ResourceFormat Format,
                                     FemeImageSubresourceLayout &Layout,
                                     uint32_t ExtraFlags = 0) {
  Layout = {0, 0, 0, 0};
  uint64_t ElemSize =
      SizeInBytes / (uint64_t)Width / (uint64_t)Height / (uint64_t)ArrayLayers;
  Layout.RowPitch = Width * ElemSize;
  Layout.SlicePitch = Layout.RowPitch * Height;

  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = SizeInBytes;
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(Format);
  Img.Width = Width;
  Img.Height = Height;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = ArrayLayers;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | ExtraFlags;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  return Img;
}

/// The roadmap H19c plain-1D counterpart of `makeImage2D` above: a
/// single-mip-level, single-layer, `Height == 1`/`Depth == 1` 1D
/// `FemeImageDescriptor` over \p Storage. `Height == 1` is exactly what
/// makes `femeRTFetchTexel1D`/`femeRTStoreTexel1D`'s own thin-wrapper
/// reuse of the existing 2D addressing math (`Y == 0`) correct -- see
/// that pair's own comment in FeMeRuntimeCPU.c.
FemeImageDescriptor makeImage1D(void *Storage, uint64_t SizeInBytes,
                                uint32_t Width, ResourceFormat Format,
                                FemeImageSubresourceLayout &Layout,
                                uint32_t ExtraFlags = 0) {
  Layout = {0, 0, 0, 0};
  uint64_t ElemSize = SizeInBytes / (uint64_t)Width;
  Layout.RowPitch = Width * ElemSize;
  Layout.SlicePitch = Layout.RowPitch;

  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = SizeInBytes;
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture1D);
  Img.Format = static_cast<uint32_t>(Format);
  Img.Width = Width;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | ExtraFlags;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  return Img;
}

/// The roadmap H19e arrayed-1D counterpart of `makeImage1D` above: a
/// single-mip-level, `Height == 1`/`Depth == 1`, `ArrayLayers`-layer 1D
/// `FemeImageDescriptor` over \p Storage (assumed row-major, tightly
/// packed, layer-major -- mirroring `makeImage2DArray`'s own per-layer
/// layout, narrowed to a single spatial `X` axis).
FemeImageDescriptor makeImage1DArray(void *Storage, uint64_t SizeInBytes,
                                     uint32_t Width, uint32_t ArrayLayers,
                                     ResourceFormat Format,
                                     FemeImageSubresourceLayout &Layout,
                                     uint32_t ExtraFlags = 0) {
  Layout = {0, 0, 0, 0};
  uint64_t ElemSize = SizeInBytes / (uint64_t)Width / (uint64_t)ArrayLayers;
  Layout.RowPitch = Width * ElemSize;
  Layout.SlicePitch = Layout.RowPitch;

  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = SizeInBytes;
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture1DArray);
  Img.Format = static_cast<uint32_t>(Format);
  Img.Width = Width;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = ArrayLayers;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | ExtraFlags;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  return Img;
}

/// The roadmap H19c plain-3D counterpart of `makeImage2D` above: a
/// single-mip-level 3D `FemeImageDescriptor` over \p Storage (assumed
/// row-major, tightly packed, slice-major -- each depth slice's own
/// texels contiguous before the next slice's, addressed via
/// `SlicePitch`, mirroring `makeImage2DArray`'s own per-layer layout).
/// `ArrayLayers` stays `1`: a real 3D image is never arrayed per the
/// Vulkan spec.
FemeImageDescriptor makeImage3D(void *Storage, uint64_t SizeInBytes,
                                uint32_t Width, uint32_t Height,
                                uint32_t Depth, ResourceFormat Format,
                                FemeImageSubresourceLayout &Layout,
                                uint32_t ExtraFlags = 0) {
  Layout = {0, 0, 0, 0};
  uint64_t ElemSize =
      SizeInBytes / (uint64_t)Width / (uint64_t)Height / (uint64_t)Depth;
  Layout.RowPitch = Width * ElemSize;
  Layout.SlicePitch = Layout.RowPitch * Height;

  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = SizeInBytes;
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture3D);
  Img.Format = static_cast<uint32_t>(Format);
  Img.Width = Width;
  Img.Height = Height;
  Img.Depth = Depth;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | ExtraFlags;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  return Img;
}

FemeSamplerDescriptor makeSampler(SamplerFilter MagFilter,
                                  SamplerAddressMode AddressMode) {
  FemeSamplerDescriptor Samp{};
  Samp.MinFilter = static_cast<uint32_t>(MagFilter);
  Samp.MagFilter = static_cast<uint32_t>(MagFilter);
  Samp.MipFilter = static_cast<uint32_t>(SamplerFilter::Nearest);
  Samp.AddressU = static_cast<uint32_t>(AddressMode);
  Samp.AddressV = static_cast<uint32_t>(AddressMode);
  Samp.AddressW = static_cast<uint32_t>(AddressMode);
  Samp.MinLod = 0.0f;
  // `VK_LOD_CLAMP_NONE`: matches how a real application requests an
  // unclamped mip range (roadmap H15's own `MinLod`/`MaxLod` clamp fix
  // would otherwise force every sample in this file down to level 0,
  // since a zero-initialized `MaxLod` is a real, valid "clamp to the
  // base level" request, not merely an unset default).
  Samp.MaxLod = 1000.0f;
  return Samp;
}

TEST_F(ImageSamplingTest, PointSampleIdentityFormat) {
  // A 2x2 R32G32B32A32_FLOAT image; point-sampling the center of texel
  // (1, 0) must read that texel exactly, with no blending from its
  // neighbors.
  float Storage[2][2][4] = {{{1, 2, 3, 4}, {5, 6, 7, 8}},
                            {{9, 10, 11, 12}, {13, 14, 15, 16}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  // Texel (1, 0)'s center is at normalized coordinates (0.75, 0.25).
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.75f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 5.0f);
  EXPECT_FLOAT_EQ(Out[1], 6.0f);
  EXPECT_FLOAT_EQ(Out[2], 7.0f);
  EXPECT_FLOAT_EQ(Out[3], 8.0f);
}

TEST_F(ImageSamplingTest, LinearSampleBlendsFourTexels) {
  // Sampling exactly at the shared corner of all four texels of a 2x2
  // image must average all four equally.
  float Storage[2][2][4] = {{{0, 0, 0, 0}, {4, 0, 0, 0}},
                            {{0, 4, 0, 0}, {4, 4, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 2.0f);
  EXPECT_FLOAT_EQ(Out[1], 2.0f);
}

TEST_F(ImageSamplingTest, ExplicitLodMinifyingUsesMinFilterNotMagFilter) {
  // Roadmap H16: a *minifying* explicit-LOD sample (`Lod > 0`) must use
  // the sampler's own `MinFilter`, not always `MagFilter`, per the
  // Vulkan spec's own magnification/minification filter-selection rule.
  // Same 2x2 image and sample point as `LinearSampleBlendsFourTexels`
  // above (whose corner-of-all-four-texels coordinate cleanly
  // distinguishes a nearest read, which reads exactly one texel, from a
  // bilinear read, which averages all four) but with `MagFilter` set to
  // `Nearest` and `MinFilter` set to `Linear`: a minifying sample here
  // must still bilinear-blend (matching `MinFilter`), even though
  // `MagFilter` says `Nearest`.
  float Storage[2][2][4] = {{{0, 0, 0, 0}, {4, 0, 0, 0}},
                            {{0, 4, 0, 0}, {4, 4, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MinFilter = static_cast<uint32_t>(SamplerFilter::Linear);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  // `Lod=1.0` (explicit, minifying: `ClampedLod > 0`).
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     /*Lod=*/1.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 2.0f);
  EXPECT_FLOAT_EQ(Out[1], 2.0f);
}

TEST_F(ImageSamplingTest, ExplicitLodMagnifyingUsesMagFilterNotMinFilter) {
  // Roadmap H16: the magnifying (`Lod <= 0`) counterpart of
  // `ExplicitLodMinifyingUsesMinFilterNotMagFilter` above -- same image,
  // same sample point, `MagFilter` and `MinFilter` swapped
  // (`MagFilter=Nearest`, `MinFilter=Linear`) -- but a negative, explicit
  // `Lod` (a genuinely magnifying sample) must instead read exactly one
  // texel (matching `MagFilter=Nearest`), not the four-texel bilinear
  // blend `MinFilter=Linear` would otherwise produce.
  float Storage[2][2][4] = {{{0, 0, 0, 0}, {4, 0, 0, 0}},
                            {{0, 4, 0, 0}, {4, 4, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MinFilter = static_cast<uint32_t>(SamplerFilter::Linear);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  // `Lod=-1.0` (explicit, magnifying: `ClampedLod <= 0`).
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     /*Lod=*/-1.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 4.0f);
  EXPECT_FLOAT_EQ(Out[1], 4.0f);
}

TEST_F(ImageSamplingTest, ImplicitLodMinifyingUsesMinFilterNotMagFilter) {
  // Roadmap H16: the implicit-LOD counterpart of
  // `ExplicitLodMinifyingUsesMinFilterNotMagFilter` above -- a caller's
  // own screen-space derivatives (not an explicit `Lod` operand) resolve
  // to a minifying LOD (a per-pixel `dU/dx` of 1.0 across this image's
  // 2-texel width is a scale factor of 2 texels/pixel, `log2(2) == 1 >
  // 0`), and that must still consult `MinFilter`, not `MagFilter`, even
  // though `femeRTPlanImplicitLod`'s own single-tap path (no anisotropy
  // configured here) is what resolves it.
  float Storage[2][2][4] = {{{0, 0, 0, 0}, {4, 0, 0, 0}},
                            {{0, 4, 0, 0}, {4, 4, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MinFilter = static_cast<uint32_t>(SamplerFilter::Linear);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/1.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 2.0f);
  EXPECT_FLOAT_EQ(Out[1], 2.0f);
}

TEST_F(ImageSamplingTest, ExplicitLodTrilinearBlendsBetweenTwoMipLevels) {
  // Roadmap H17: a `mipmapMode=LINEAR` (`MipFilter=Linear`) sampler must
  // blend the two adjacent mip levels an explicit `Lod` falls between,
  // weighted by `Lod`'s own fractional part -- not round to a single
  // nearest level the way a `mipmapMode=NEAREST` sampler does. Same
  // two-level image as `ImplicitLodSelectsCoarserMipFromDerivatives`
  // (level 0 uniformly `1.0`, level 1 uniformly `9.0`): an explicit
  // `Lod=0.5` must read exactly the midpoint, `5.0`.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MipFilter = static_cast<uint32_t>(SamplerFilter::Linear);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     /*Lod=*/0.5f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 5.0f);
}

TEST_F(ImageSamplingTest, ExplicitLodNearestMipFilterStillRoundsToOneLevel) {
  // Roadmap H17 regression: the pre-H17 behavior (round to a single
  // nearest level, no trilinear blend) must be preserved for
  // `mipmapMode=NEAREST` (`MipFilter=Nearest`, `makeSampler`'s own
  // default). Same image and `Lod=0.5` as
  // `ExplicitLodTrilinearBlendsBetweenTwoMipLevels` above, but this
  // sampler's own default `MipFilter=Nearest` must instead round
  // `Lod=0.5` up to level 1 and read `9.0` outright, not blend.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     /*Lod=*/0.5f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, ImplicitLodTrilinearBlendsBetweenTwoMipLevels) {
  // Roadmap H17: the implicit-LOD counterpart of
  // `ExplicitLodTrilinearBlendsBetweenTwoMipLevels` above -- a caller's
  // own screen-space derivatives (not an explicit `Lod` operand) resolve
  // to a LOD strictly between the two levels (a per-pixel `dU/dx` of
  // `sqrt(2)/2` across this image's 2-texel width is a scale factor of
  // `sqrt(2)` texels/pixel, `log2(sqrt(2)) == 0.5`), and a `MipFilter=
  // Linear` sampler must blend both levels rather than read either one
  // outright -- the result must land strictly between the two levels'
  // own values (not equal to either), unlike a `MipFilter=Nearest`
  // sampler's single-level read. (`femeRTFastLog2`'s own approximation
  // error keeps the resolved LOD from landing at exactly `0.5`, so this
  // asserts the blend occurred at all, rather than pinning an exact
  // value a closed-form `log2` would produce.)
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MipFilter = static_cast<uint32_t>(SamplerFilter::Linear);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/0.70710678f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_GT(Out[0], 1.0f);
  EXPECT_LT(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, RepeatAddressingWrapsCoordinate) {
  // Sampling just past the right edge with Repeat addressing must wrap
  // around to the left column.
  float Storage[1][2][4] = {{{1, 1, 1, 1}, {9, 9, 9, 9}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::Repeat);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  // 1.25 wraps to 0.25, texel 0's center: reads the first (value-1) texel.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.25f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
}

TEST_F(ImageSamplingTest, ClampToBorderReadsBorderColor) {
  float Storage[1][1][4] = {{{1, 1, 1, 1}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToBorder);
  Samp.BorderColor[0] = 0.1f;
  Samp.BorderColor[1] = 0.2f;
  Samp.BorderColor[2] = 0.3f;
  Samp.BorderColor[3] = 0.4f;
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 2.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.1f);
  EXPECT_FLOAT_EQ(Out[1], 0.2f);
  EXPECT_FLOAT_EQ(Out[2], 0.3f);
  EXPECT_FLOAT_EQ(Out[3], 0.4f);
}

// (Roadmap L125(d)) A non-identity `FemeImageDescriptor::Swizzle` must
// apply to a synthesized border color exactly as it would to an in-bounds
// texel, mirroring core Vulkan's own "conversion to RGBA" rule for
// `VK_BORDER_COLOR_*_TRANSPARENT_BLACK`/`*_OPAQUE_WHITE` (no
// `VK_EXT_border_color_swizzle` needed for those two -- see
// `vktPipelineSamplerBorderSwizzleTests.cpp`'s own gating, upstream
// VK-GL-CTS). Uses a `BARG` mapping (output R reads input B, output G
// reads input A, output B reads input R, output A is the constant `Zero`)
// against a distinguishable `{0.1, 0.2, 0.3, 0.4}` border color: every
// output channel should differ from its identity-mapped counterpart, so a
// regression that silently drops `Swizzle` (reverting to identity) cannot
// coincidentally still pass.
TEST_F(ImageSamplingTest, ClampToBorderAppliesImageViewSwizzle) {
  float Storage[1][1][4] = {{{1, 1, 1, 1}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::B, ImageComponentSwizzle::A,
      ImageComponentSwizzle::R, ImageComponentSwizzle::Zero);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToBorder);
  Samp.BorderColor[0] = 0.1f;
  Samp.BorderColor[1] = 0.2f;
  Samp.BorderColor[2] = 0.3f;
  Samp.BorderColor[3] = 0.4f;
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 2.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.3f); // R <- B
  EXPECT_FLOAT_EQ(Out[1], 0.4f); // G <- A
  EXPECT_FLOAT_EQ(Out[2], 0.1f); // B <- R
  EXPECT_FLOAT_EQ(Out[3], 0.0f); // A <- Zero
}

// (Roadmap L125(e)) A synthesized border color's components missing from
// the *sampled image's own* format must be re-defaulted the same way an
// in-bounds texel of that format would be, before any swizzle applies --
// core Vulkan's "conversion to RGBA" rule. `R32G32B32_FLOAT` has no alpha
// channel, so `VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK`'s nominal all-zero
// border must sample with alpha forced to `1.0`, exactly like a real
// in-bounds `R32G32B32_FLOAT` texel would (see `femeRTUnpackImageTexel`'s
// own `R32G32B32_FLOAT` case) -- even though the `FemeSamplerDescriptor`
// itself still bakes a literal `0.4` in `BorderColor[3]` below, since a
// real `VkSampler` has no format to consult at creation time.
TEST_F(ImageSamplingTest, ClampToBorderExpandsMissingComponentsForFormat) {
  float Storage[1][1][3] = {{{1, 1, 1}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32G32B32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToBorder);
  Samp.BorderColor[0] = 0.1f;
  Samp.BorderColor[1] = 0.2f;
  Samp.BorderColor[2] = 0.3f;
  Samp.BorderColor[3] = 0.4f; // Baked, format-independent -- must be
                              // overridden to 1.0 at fetch time.
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 2.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.1f);
  EXPECT_FLOAT_EQ(Out[1], 0.2f);
  EXPECT_FLOAT_EQ(Out[2], 0.3f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f); // Defaulted, not the sampler's baked 0.4.
}

// (Roadmap L125(f)) Unlike a synthesized border color (L125(d)/(e) above),
// an in-bounds sampled texel previously bypassed `femeRTApplyImageSwizzle`
// entirely -- this is the first shape (`Sample2D`, nearest filtering) to
// close that gap. Uses the same `BARG` mapping and distinguishable-channel
// philosophy as `ClampToBorderAppliesImageViewSwizzle` above, but samples
// a real, in-bounds texel (`U=V=0.5` on a single-texel image, never
// touching the address mode's `ClampToBorder` path) so a regression that
// only re-breaks the in-bounds branch (while leaving the already-covered
// border branch correct) cannot coincidentally still pass.
TEST_F(ImageSamplingTest, SampleAppliesImageViewSwizzleToInBoundsTexel) {
  float Storage[1][1][4] = {{{0.1f, 0.2f, 0.3f, 0.4f}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::B, ImageComponentSwizzle::A,
      ImageComponentSwizzle::R, ImageComponentSwizzle::Zero);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.3f); // R <- B
  EXPECT_FLOAT_EQ(Out[1], 0.4f); // G <- A
  EXPECT_FLOAT_EQ(Out[2], 0.1f); // B <- R
  EXPECT_FLOAT_EQ(Out[3], 0.0f); // A <- Zero
}

// (Roadmap L125(f)) The mirror-image regression test: a storage-image
// load (`feme.cpu.image.load.2d.v4f32`, Vulkan's `OpImageRead`) must
// *never* apply `FemeImageDescriptor::Swizzle`, even when non-identity --
// per the Vulkan spec, only a sampled-image access (`OpImageSample*`/
// `OpImageFetch`) honors an image view's own `VkComponentMapping`.
// Protects the storage-image semantics `femeRTFetchTexel2D`'s own new
// `ApplySwizzle` parameter is meant to preserve going forward.
TEST_F(ImageSamplingTest, LoadNeverAppliesImageViewSwizzle) {
  float Storage[1][1][4] = {{{0.1f, 0.2f, 0.3f, 0.4f}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::B, ImageComponentSwizzle::A,
      ImageComponentSwizzle::R, ImageComponentSwizzle::Zero);
  FemeImageDescriptor ImageHeap[1] = {Img};

  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.1f); // Unswizzled: raw R.
  EXPECT_FLOAT_EQ(Out[1], 0.2f); // Unswizzled: raw G.
  EXPECT_FLOAT_EQ(Out[2], 0.3f); // Unswizzled: raw B.
  EXPECT_FLOAT_EQ(Out[3], 0.4f); // Unswizzled: raw A.
}

// (Roadmap L125(h)) The integer-sampled (`*.v4i32`) counterpart of
// `SampleAppliesImageViewSwizzleToInBoundsTexel` above: an integer-format
// image (`R32G32B32A32_SINT`) sampled via `feme.cpu.image.sample.2d.v4i32`
// (always nearest-filtered, explicit-LOD-only per that call kind's own
// doc) must also apply the image view's own component swizzle to an
// in-bounds texel, exactly like the float path -- this is a wholly
// separate, non-shared function family from `femeRTFetchTexel2D`, so
// this is this family's own first swizzle-correctness test rather than a
// shared regression guard.
TEST_F(ImageSamplingTest, SampleI32AppliesImageViewSwizzleToInBoundsTexel) {
  int32_t Storage[1][1][4] = {{{1, 2, 3, 4}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32G32B32A32_SINT,
      Layout);
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::B, ImageComponentSwizzle::A,
      ImageComponentSwizzle::R, ImageComponentSwizzle::Zero);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleI32Fn Fn = resolve<SampleI32Fn>(
      addWrapper("sample_i32", "feme.cpu.image.sample.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*Lod=*/0.0f,
     /*OffsetX=*/0, /*OffsetY=*/0, true, Out);
  EXPECT_EQ(Out[0], 3); // R <- B
  EXPECT_EQ(Out[1], 4); // G <- A
  EXPECT_EQ(Out[2], 1); // B <- R
  EXPECT_EQ(Out[3], 0); // A <- Zero
}

// (Roadmap L125(q)) Unlike the float path (`ClampToBorderAppliesImage
// ViewSwizzle` above), the integer-sampled `CLAMP_TO_BORDER` fallback
// previously returned its border default unswizzled, bypassing the image
// view's own component mapping entirely. Confirmed via a real CTS
// regression
// (`sampler.border_swizzle.r16_sint.barg.transparent_black.no_gather.*`)
// that this is reachable, not merely a theoretical gap. `U`/`V` far
// outside `[0, 1]` force every axis onto the border. Uses
// `VK_BORDER_COLOR_INT_OPAQUE_BLACK` (`BorderColor = {0, 0, 0, 1}`,
// set explicitly rather than relying on `makeSampler`'s own
// `transparent_black`-shaped zero-initialized default) so the border's
// own non-identity per-channel pattern -- not just `femeRTImageFormat
// ComponentMask`'s forced-fill defaults, see
// `SampleI32BorderColorFallbackVariesByFormatAndBorderColor` below for
// that -- is what this test's swizzle-order assertions exercise, against
// a 4-real-channel format (`R32G32B32A32_SINT`) where no component is
// format-masked at all.
TEST_F(ImageSamplingTest, SampleI32AppliesImageViewSwizzleToBorderColorFallback) {
  int32_t Storage[1][1][4] = {{{9, 9, 9, 9}}}; // Never read: always border.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32G32B32A32_SINT,
      Layout);
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::B, ImageComponentSwizzle::A,
      ImageComponentSwizzle::R, ImageComponentSwizzle::Zero);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp = makeSampler(SamplerFilter::Nearest,
                                           SamplerAddressMode::ClampToBorder);
  Samp.BorderColor[3] = 1.0f; // VK_BORDER_COLOR_INT_OPAQUE_BLACK.
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleI32Fn Fn = resolve<SampleI32Fn>(
      addWrapper("sample_i32_border", "feme.cpu.image.sample.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 5.0f, 5.0f, /*Lod=*/0.0f,
     /*OffsetX=*/0, /*OffsetY=*/0, true, Out);
  EXPECT_EQ(Out[0], 0); // R <- B (pre-swizzle border B is 0)
  EXPECT_EQ(Out[1], 1); // G <- A (pre-swizzle border A is 1)
  EXPECT_EQ(Out[2], 0); // B <- R (pre-swizzle border R is 0)
  EXPECT_EQ(Out[3], 0); // A <- Zero
}

// (Roadmap L125(v)) Unlike the fix above (which only corrected swizzle
// *order*, still against a fixed `{0, 0, 0, 1}` border default), this
// confirms the border default itself now varies by the sampler's own
// `BorderColor` and by the *image format*'s stored-component count, not a
// hardcoded literal. `R16_SINT` stores only 1 real component (R); with
// `opaque_white` (`BorderColor = {1, 1, 1, 1}`), Vulkan's border-color
// "conversion to RGBA" rule keeps only R from the border color's own
// nominal value and forces G/B to `0`/A to `1` regardless of the border
// color's own nominal G/B/A -- pre-swizzle border is therefore
// `(1, 0, 0, 1)`, not `opaque_white`'s own nominal `(1, 1, 1, 1)`.
// Confirmed via a real CTS regression
// (`sampler.border_swizzle.r16_sint.argb.opaque_white.no_gather.*`,
// `Ref:(1, 1, 0, 0)` vs. the pre-fix `Color:(1, 0, 0, 0)`).
TEST_F(ImageSamplingTest,
       SampleI32BorderColorFallbackVariesByFormatAndBorderColor) {
  int16_t Storage[1][1] = {{9}}; // Never read: always border.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                       ResourceFormat::R16_SINT, Layout);
  // `argb` component mapping: R<-A, G<-R, B<-G, A<-B.
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::A, ImageComponentSwizzle::R,
      ImageComponentSwizzle::G, ImageComponentSwizzle::B);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp = makeSampler(SamplerFilter::Nearest,
                                           SamplerAddressMode::ClampToBorder);
  Samp.BorderColor[0] = Samp.BorderColor[1] = Samp.BorderColor[2] =
      Samp.BorderColor[3] = 1.0f; // VK_BORDER_COLOR_INT_OPAQUE_WHITE.
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleI32Fn Fn = resolve<SampleI32Fn>(addWrapper(
      "sample_i32_border_opaque_white", "feme.cpu.image.sample.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 5.0f, 5.0f, /*Lod=*/0.0f,
     /*OffsetX=*/0, /*OffsetY=*/0, true, Out);
  EXPECT_EQ(Out[0], 1); // R <- A (pre-swizzle border A is 1)
  EXPECT_EQ(Out[1], 1); // G <- R (pre-swizzle border R is 1)
  EXPECT_EQ(Out[2], 0); // B <- G (pre-swizzle border G is forced 0)
  EXPECT_EQ(Out[3], 0); // A <- B (pre-swizzle border B is forced 0)
}

// (Roadmap L125(h)) The integer-sampled counterpart of
// `LoadNeverAppliesImageViewSwizzle` above: `feme.cpu.image.load.2d.v4i32`
// (Vulkan's `OpImageRead` against a storage image) must never apply
// `FemeImageDescriptor::Swizzle` either, exactly like its float
// counterpart.
TEST_F(ImageSamplingTest, LoadI32NeverAppliesImageViewSwizzle) {
  int32_t Storage[1][1][4] = {{{1, 2, 3, 4}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32G32B32A32_SINT,
      Layout);
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::B, ImageComponentSwizzle::A,
      ImageComponentSwizzle::R, ImageComponentSwizzle::Zero);
  FemeImageDescriptor ImageHeap[1] = {Img};

  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 1); // Unswizzled: raw R.
  EXPECT_EQ(Out[1], 2); // Unswizzled: raw G.
  EXPECT_EQ(Out[2], 3); // Unswizzled: raw B.
  EXPECT_EQ(Out[3], 4); // Unswizzled: raw A.
}

TEST_F(ImageSamplingTest, SRGBDecodeOnSample) {
  // A single R8G8B8A8_UNORM_SRGB texel with R=G=B=188/255 (~0.7372549), the
  // sRGB encoding of linear 0.5 (matching sRGB's well-known midpoint
  // round-trip value); alpha is 255 (1.0) and must stay exactly 1.0
  // (never sRGB-decoded). Byte order is little-endian: R, G, B, A.
  uint32_t Storage[1][1] = {{0xFFBCBCBCu}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R8G8B8A8_UNORM_SRGB, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_NEAR(Out[0], 0.5f, 0.01f);
  EXPECT_NEAR(Out[1], 0.5f, 0.01f);
  EXPECT_NEAR(Out[2], 0.5f, 0.01f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f); // Alpha is never sRGB-decoded.
}

TEST_F(ImageSamplingTest, ExplicitLodSelectsMipLevel) {
  // A two-level mip chain: level 0 is 2x2 (all 1s), level 1 is 1x1 (value
  // 9). Sampling level 1 explicitly must read the coarser level, not the
  // base.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, ImplicitLodWithNoDerivativesReadsBaseLevel) {
  // Roadmap H7i: an implicit-LOD sample (`UseExplicitLod` false) given no
  // screen-space derivatives at all (every `D*` operand zero, e.g. a
  // caller outside the fragment stage) must still read the base level --
  // no measurable minification was reported, so `femeRTPlanImplicitLod`
  // resolves to level 0 exactly as this sample always did before this
  // row.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
}

TEST_F(ImageSamplingTest, ImplicitLodBiasSelectsCoarserMipLevel) {
  // Roadmap L58: a nonzero `Bias` operand (SPIR-V's own ordinary,
  // non-comparison `Bias` image operand, e.g. HLSL's
  // `Texture2D::Sample`'s trailing `bias` argument) is added directly
  // into the resolved LOD alongside the sampler's own static `LodBias`
  // and any screen-space-derivative contribution. With zero screen-space
  // derivatives (no measurable minification of its own), a `Bias` of
  // exactly `1.0` must select mip level 1 outright (`MipFilter=Nearest`
  // rounds `0 + 1.0` up to level 1), unlike
  // `ImplicitLodWithNoDerivativesReadsBaseLevel`'s own zero-bias case,
  // which reads level 0 in the same configuration.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Bias=*/1.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, ImplicitLodSelectsCoarserMipFromDerivatives) {
  // Roadmap H7i: an implicit-LOD sample now derives a real mip level from
  // the caller's own screen-space partial derivatives of (U, V), instead
  // of always reading level 0. A per-pixel `dU/dx` of 1.0 across this
  // image's 2-texel width is a scale factor of 2 texels/pixel -- `log2(2)
  // == 1` -- so this sample must read level 1, the coarser of the two.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/1.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, MaxLodClampsImplicitSampleToBaseLevel) {
  // Roadmap H15: `dEQP-VK.texture.filtering.2d.combinations.*` (and any
  // other case using a real, non-mipmapped `VkFilter`) sets the
  // sampler's own `maxLod` to a small clamp (`0.25`, mirroring
  // `vkImageUtil.cpp`'s own `mapSampler` for a `tcu::Sampler::NEAREST`/
  // `LINEAR`/`CUBIC` min filter) specifically to force every implicit-LOD
  // sample to the base level regardless of how minified the footprint
  // actually is -- this is the same (U, V) and derivatives as
  // `ImplicitLodSelectsCoarserMipFromDerivatives` above (which reads the
  // coarser level 1 with an unclamped `MaxLod`), but a `MaxLod` of `0.25`
  // here must instead clamp back down to level 0.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MaxLod = 0.25f;
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/1.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
}

TEST_F(ImageSamplingTest, MinLodClampsExplicitSampleAboveBaseLevel) {
  // Roadmap H15: the same `MinLod`/`MaxLod` clamp applies to an
  // explicit-LOD sample too (the Vulkan spec's own "lod = clamp(lod +
  // mipLodBias, minLod, maxLod)" step does not distinguish an implicit
  // from an explicit source for `lod`) -- an explicit `Lod` of `0.0`
  // (which would ordinarily read the base level) must clamp up to level
  // 1 when `MinLod` excludes level 0 entirely.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MinLod = 1.0f;
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, LodBiasShiftsSelectedLevel) {
  // Roadmap H15: `Samp->LodBias` (`VkSamplerCreateInfo::mipLodBias`) adds
  // into the level-of-detail computation before the `MinLod`/`MaxLod`
  // clamp -- an explicit `Lod` of `0.0` with a `LodBias` of `1.0` must
  // read level 1, exactly as if the caller had passed `Lod=1.0` outright.
  float Level0[2][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}},
                           {{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.LodBias = 1.0f;
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, AnisotropicSampleDiffersFromIsotropicSample) {
  // Roadmap H7i: this is the exact shape
  // `dEQP-VK.texture.filtering.2d.*anisotropy*`'s own test logic requires
  // (vktTextureFilteringAnisotropyTests.cpp's `FilteringAnisotropyInstance`)
  // -- the same (U, V) and derivatives, sampled once with anisotropic
  // filtering enabled and once without, must read a measurably different
  // result; an inert `MaxAnisotropy` would make the two reads identical
  // and fail that real CTS assertion. A single-column (1x8) image whose
  // eight texels hold distinct values along V, sampled with a footprint
  // whose derivatives are elongated entirely along V (`DVdY` nonzero,
  // every other derivative zero -- a surface viewed edge-on along U, the
  // classic anisotropic case), exercises this directly: an isotropic
  // (`MaxAnisotropy` disabled) sample reads exactly one texel, while an
  // anisotropic one averages several taps spread along that footprint.
  float Storage[8][1][4];
  for (int I = 0; I != 8; ++I)
    Storage[I][0][0] = Storage[I][0][1] = Storage[I][0][2] =
        Storage[I][0][3] = (float)I;

  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), /*Width=*/1, /*Height=*/8,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};

  FemeSamplerDescriptor Isotropic =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor Anisotropic = Isotropic;
  Anisotropic.Flags |= FEME_SAMPLER_ANISOTROPY_ENABLE;
  Anisotropic.MaxAnisotropy = 4.0f;

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));

  FemeSamplerDescriptor IsotropicHeap[1] = {Isotropic};
  float IsotropicOut[4];
  Fn(ImageHeap, 1, IsotropicHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.5f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, IsotropicOut);

  FemeSamplerDescriptor AnisotropicHeap[1] = {Anisotropic};
  float AnisotropicOut[4];
  Fn(ImageHeap, 1, AnisotropicHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.5f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, AnisotropicOut);

  // The isotropic sample reads exactly one texel (row 4); the anisotropic
  // one averages four taps spread across rows 2-5, a measurably different
  // (lower) value -- exactly the divergence the real CTS case requires.
  EXPECT_FLOAT_EQ(IsotropicOut[0], 4.0f);
  EXPECT_FLOAT_EQ(AnisotropicOut[0], 3.5f);
  EXPECT_NE(IsotropicOut[0], AnisotropicOut[0]);
}

TEST_F(ImageSamplingTest, ComparisonSamplingLessEqualPasses) {
  // A single depth texel of 0.5: comparing a reference of 0.4 with
  // LessEqual must fail (0.4 <= 0.5 is actually true -- see below), so
  // pick values that make both a clear pass and a clear fail case
  // unambiguous.
  float Storage[1][1][4] = {{{0.5f, 0, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpFn Fn = resolve<SampleCmpFn>(
      addWrapper("samplecmp", "feme.cpu.image.samplecmp.2d.f32"));
  float PassResult = 0.0f, FailResult = 1.0f;
  // Ref (0.4) <= Texel (0.5): pass.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, true, 0.4f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &PassResult);
  EXPECT_FLOAT_EQ(PassResult, 1.0f);
  // Ref (0.6) <= Texel (0.5): fail.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, true, 0.6f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &FailResult);
  EXPECT_FLOAT_EQ(FailResult, 0.0f);
}

TEST_F(ImageSamplingTest, ComparisonSamplingBiasSelectsCoarserMipLevel) {
  // Roadmap L52(b): a nonzero `Bias` operand on a *depth-comparison*
  // sample feeds the same implicit-LOD resolution an ordinary sample's
  // own `Bias` does (see `ImplicitLodBiasSelectsCoarserMipLevel`), so it
  // must change which mip level the reference is compared against.
  //
  // Level 0 stores a depth of 0.2 and level 1 a depth of 0.8. With a
  // `LessEqual` comparison and a reference of 0.5, level 0 fails
  // (0.5 <= 0.2 is false) while level 1 passes (0.5 <= 0.8 is true), so
  // the two levels are distinguishable purely from the comparison result.
  float Level0[2][2][4] = {{{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}},
                           {{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}}};
  float Level1[1][1][4] = {{{0.8f, 0, 0, 0}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | FEME_IMAGE_DEPTH;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};

  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpFn Fn = resolve<SampleCmpFn>(
      addWrapper("samplecmp", "feme.cpu.image.samplecmp.2d.f32"));

  // No bias: the base level's own 0.2 fails the 0.5 reference.
  float Unbiased = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &Unbiased);
  EXPECT_FLOAT_EQ(Unbiased, 0.0f);

  // A bias of exactly 1.0 rounds the resolved LOD up to level 1, whose
  // own 0.8 passes the same reference.
  float Biased = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/1.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &Biased);
  EXPECT_FLOAT_EQ(Biased, 1.0f);
}

TEST_F(ImageSamplingTest, ComparisonSamplingGradSelectsCoarserMipLevel) {
  // Roadmap L66(c): a real, nonzero `DUdX`/`DUdY`/`DVdX`/`DVdY` screen-
  // space derivative quartet on a depth-comparison sample feeds the same
  // `femeRTPlanImplicitLod` resolution an ordinary sample's own
  // derivatives do (mirroring `Sample1DRealDerivativeSelectsCoarserMipLevel`'s
  // own precedent), so it must change which mip level the reference is
  // compared against -- exactly the same observable effect
  // `ComparisonSamplingBiasSelectsCoarserMipLevel` above proves for
  // `Bias`, but driven by a real derivative instead.
  //
  // Same two-level depth image as
  // `ComparisonSamplingBiasSelectsCoarserMipLevel` above: level 0 stores 0.2,
  // level 1 stores 0.8; a `LessEqual` compare against a reference of 0.5 fails
  // at level 0 and passes at level 1.
  float Level0[2][2][4] = {{{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}},
                           {{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}}};
  float Level1[1][1][4] = {{{0.8f, 0, 0, 0}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | FEME_IMAGE_DEPTH;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};

  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpFn Fn = resolve<SampleCmpFn>(
      addWrapper("samplecmp", "feme.cpu.image.samplecmp.2d.f32"));

  // No derivatives: the base level's own 0.2 fails the 0.5 reference,
  // exactly like `ComparisonSamplingBiasSelectsCoarserMipLevel`'s own
  // `Unbiased` case (proving zero derivatives still degenerate to level
  // 0, the pre-L66(c) behavior every non-`Grad` caller still gets).
  float NoGrad = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &NoGrad);
  EXPECT_FLOAT_EQ(NoGrad, 0.0f);

  // A `DUdX` of exactly 1.0 against this 2-texel-wide image resolves to
  // an implicit LOD of exactly 1.0 (`femeRTPlanImplicitLod`'s own
  // `Px = DUdX * Width = 2.0`, `Lod = log2(2.0) = 1.0`), rounding the
  // selected mip level up to level 1, whose own 0.8 passes the same
  // reference.
  float WithGrad = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/1.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &WithGrad);
  EXPECT_FLOAT_EQ(WithGrad, 1.0f);
}

TEST_F(ImageSamplingTest, ComparisonSamplingNonzeroOffsetShiftsFetchedTexel) {
  // Roadmap L50d: a real, nonzero `ConstOffset` (`OffsetX`/`OffsetY`) must
  // shift which depth texel is fetched and compared against `Dref`, the
  // same way it already shifts an ordinary color sample's own fetched
  // texel (roadmap L26) -- a 2x1 depth image whose two texels straddle a
  // comparison reference of 0.5 (texel 0 is 0.4, below; texel 1 is 0.6,
  // above) makes a `(+1, 0)` offset applied to a nearest-point sample
  // pinned at texel 0's own coordinate flip a `LessEqual` compare's
  // pass/fail outcome, unambiguously proving the offset was actually
  // applied rather than silently dropped.
  float Storage[1][2][4] = {{{0.4f, 0, 0, 0}, {0.6f, 0, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), /*Width=*/2, /*Height=*/1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpFn Fn = resolve<SampleCmpFn>(
      addWrapper("samplecmp", "feme.cpu.image.samplecmp.2d.f32"));
  // No offset: (0.25, 0.5) reads texel 0 (0.4). Ref (0.5) <= 0.4: fail.
  float NoOffsetResult = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, true, 0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &NoOffsetResult);
  EXPECT_FLOAT_EQ(NoOffsetResult, 0.0f);
  // A `(+1, 0)` offset shifts the same (0.25, 0.5) coordinate's own
  // fetched texel to texel 1 (0.6). Ref (0.5) <= 0.6: pass.
  float OffsetResult = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, true, 0.5f, /*Bias=*/0.0f, 1, 0,
     -std::numeric_limits<float>::infinity(), true, &OffsetResult);
  EXPECT_FLOAT_EQ(OffsetResult, 1.0f);
}

TEST_F(ImageSamplingTest, GatherCmpReturnsFourTexelsInDrefGatherOrder) {
  // Roadmap L7d: `feme.cpu.image.gathercmp.2d.v4f32` returns one
  // comparison result per corner of the bilinear-filter footprint at the
  // sampled coordinate, packed in the real SPIR-V/HLSL
  // `OpImageDrefGather` component order derived from `dxc`'s own real
  // repro: `[C(X0,Y1), C(X1,Y1), C(X1,Y0), C(X0,Y0)]` -- reusing the same
  // 2x2-image, shared-corner coordinate setup
  // `LinearSampleBlendsFourTexels` uses, but with a distinct depth value
  // in every one of the four texels so a wrong ordering shows up as a
  // wrong result rather than a coincidentally-correct one.
  float Storage[2][2][4] = {{{0.4f, 0, 0, 0}, {0.6f, 0, 0, 0}},
                            {{0.3f, 0, 0, 0}, {0.7f, 0, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCmpFn Fn = resolve<GatherCmpFn>(
      addWrapper("gathercmp", "feme.cpu.image.gathercmp.2d.v4f32"));
  float Out[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  // Ref (0.5) <= Texel: T(X0,Y0)=0.4 fails, T(X1,Y0)=0.6 passes,
  // T(X0,Y1)=0.3 fails, T(X1,Y1)=0.7 passes.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.5f, 0, 0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f); // C(X0,Y1)
  EXPECT_FLOAT_EQ(Out[1], 1.0f); // C(X1,Y1)
  EXPECT_FLOAT_EQ(Out[2], 1.0f); // C(X1,Y0)
  EXPECT_FLOAT_EQ(Out[3], 0.0f); // C(X0,Y0)
}

TEST_F(ImageSamplingTest, GatherCmpNonzeroOffsetShiftsFetchedFootprint) {
  // Roadmap L7d: mirroring
  // `ComparisonSamplingNonzeroOffsetShiftsFetchedTexel`'s own identical
  // proof technique for the depth-comparison *sample* sibling, a real,
  // nonzero `(OffsetX, OffsetY)` must shift which four texels a gather's
  // own footprint is computed around.
  float Storage[1][3][4] = {
      {{0.4f, 0, 0, 0}, {0.6f, 0, 0, 0}, {0.9f, 0, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), /*Width=*/3, /*Height=*/1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCmpFn Fn = resolve<GatherCmpFn>(
      addWrapper("gathercmp", "feme.cpu.image.gathercmp.2d.v4f32"));
  // No offset: (1/3, 0.5) sits at the shared corner of texel 0 (0.4) and
  // texel 1 (0.6). Ref (0.5) <= 0.4: fail; Ref (0.5) <= 0.6: pass.
  float NoOffset[4] = {-1, -1, -1, -1};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f / 3.0f, 0.5f, 0.5f, 0, 0, true,
     NoOffset);
  EXPECT_FLOAT_EQ(NoOffset[2], 1.0f); // C(X1,Y0) == texel 1 (0.6): pass.
  EXPECT_FLOAT_EQ(NoOffset[3], 0.0f); // C(X0,Y0) == texel 0 (0.4): fail.
  // A `(+1, 0)` offset shifts the same coordinate's own footprint one
  // texel over, to texel 1 (0.6) / texel 2 (0.9): both now pass.
  float Offset[4] = {-1, -1, -1, -1};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f / 3.0f, 0.5f, 0.5f, 1, 0, true,
     Offset);
  EXPECT_FLOAT_EQ(Offset[2], 1.0f); // C(X1,Y0) == texel 2 (0.9): pass.
  EXPECT_FLOAT_EQ(Offset[3], 1.0f); // C(X0,Y0) == texel 1 (0.6): pass.
}

TEST_F(ImageSamplingTest, GatherReturnsFourTexelsInGatherOrder) {
  // Roadmap L7g: `feme.cpu.image.gather.2d.v4f32` returns one selected
  // `Component` channel per corner of the bilinear-filter footprint at
  // the sampled coordinate, packed in the same real SPIR-V/HLSL
  // `OpImageGather` component order `GatherCmpReturnsFourTexelsInDref
  // GatherOrder` already confirmed for the depth-comparison sibling:
  // `[T(X0,Y1), T(X1,Y1), T(X1,Y0), T(X0,Y0)]`. Reuses the same 2x2-image
  // setup, but with a distinct *green* channel value in every texel (and
  // a distinct, easily-confused red channel value) so gathering
  // `Component=1` (green) rather than the default red shows up as a
  // wrong result if the component selector is ignored.
  float Storage[2][2][4] = {{{9.0f, 0.4f, 0, 0}, {9.0f, 0.6f, 0, 0}},
                            {{9.0f, 0.3f, 0, 0}, {9.0f, 0.7f, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 2, 2, ResourceFormat::R32G32B32A32_FLOAT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherFn Fn =
      resolve<GatherFn>(addWrapper("gather", "feme.cpu.image.gather.2d.v4f32"));
  float Out[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  // Component 1 (green): T(X0,Y0)=0.4, T(X1,Y0)=0.6, T(X0,Y1)=0.3,
  // T(X1,Y1)=0.7.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*Component=*/1, 0, 0,
     true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.3f); // T(X0,Y1)
  EXPECT_FLOAT_EQ(Out[1], 0.7f); // T(X1,Y1)
  EXPECT_FLOAT_EQ(Out[2], 0.6f); // T(X1,Y0)
  EXPECT_FLOAT_EQ(Out[3], 0.4f); // T(X0,Y0)
}

TEST_F(ImageSamplingTest, GatherNonzeroOffsetShiftsFetchedFootprint) {
  // Roadmap L7g: mirroring `GatherCmpNonzeroOffsetShiftsFetchedFootprint`'s
  // own identical proof technique, a real, nonzero `(OffsetX, OffsetY)`
  // must shift which four texels a plain gather's own footprint is
  // computed around.
  float Storage[1][3][4] = {
      {{0, 0.4f, 0, 0}, {0, 0.6f, 0, 0}, {0, 0.9f, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), /*Width=*/3, /*Height=*/1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherFn Fn =
      resolve<GatherFn>(addWrapper("gather", "feme.cpu.image.gather.2d.v4f32"));
  // No offset: (1/3, 0.5) sits at the shared corner of texel 0 (0.4) and
  // texel 1 (0.6).
  float NoOffset[4] = {-1, -1, -1, -1};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f / 3.0f, 0.5f, /*Component=*/1, 0,
     0, true, NoOffset);
  EXPECT_FLOAT_EQ(NoOffset[2], 0.6f); // T(X1,Y0) == texel 1.
  EXPECT_FLOAT_EQ(NoOffset[3], 0.4f); // T(X0,Y0) == texel 0.
  // A `(+1, 0)` offset shifts the same coordinate's own footprint one
  // texel over, to texel 1 (0.6) / texel 2 (0.9).
  float Offset[4] = {-1, -1, -1, -1};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f / 3.0f, 0.5f, /*Component=*/1, 1,
     0, true, Offset);
  EXPECT_FLOAT_EQ(Offset[2], 0.9f); // T(X1,Y0) == texel 2.
  EXPECT_FLOAT_EQ(Offset[3], 0.6f); // T(X0,Y0) == texel 1.
}

// (Roadmap L125(g)) `feme.cpu.image.gather.2d.v4f32` must swizzle each
// gathered neighbor texel through the image view's own `VkComponentMapping`
// *before* `Component` selects a channel from it -- per the Vulkan spec's
// "Texel Gathering" section ("Each texel is then converted to an RGBA
// value according to component substitution and then swizzled"), mirroring
// `SampleAppliesImageViewSwizzleToInBoundsTexel`'s own proof technique but
// for the gather path. A raw-red/raw-green swap swizzle plus
// `Component=1` (green) should read what's stored in the *red* channel.
TEST_F(ImageSamplingTest, GatherAppliesImageViewSwizzleToEachTexel) {
  float Storage[2][2][4] = {{{0.4f, 9.0f, 0, 0}, {0.6f, 9.0f, 0, 0}},
                            {{0.3f, 9.0f, 0, 0}, {0.7f, 9.0f, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 2, 2, ResourceFormat::R32G32B32A32_FLOAT,
      Layout);
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::G, ImageComponentSwizzle::R,
      ImageComponentSwizzle::B, ImageComponentSwizzle::A);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherFn Fn =
      resolve<GatherFn>(addWrapper("gather", "feme.cpu.image.gather.2d.v4f32"));
  float Out[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  // Post-swizzle green (`Component=1`) reads the raw red channel: T(X0,Y0)
  // =0.4, T(X1,Y0)=0.6, T(X0,Y1)=0.3, T(X1,Y1)=0.7.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*Component=*/1, 0, 0,
     true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.3f); // T(X0,Y1)
  EXPECT_FLOAT_EQ(Out[1], 0.7f); // T(X1,Y1)
  EXPECT_FLOAT_EQ(Out[2], 0.6f); // T(X1,Y0)
  EXPECT_FLOAT_EQ(Out[3], 0.4f); // T(X0,Y0)
}

// (Roadmap L125(k)) `feme.cpu.image.gather.2d.v4i32` -- the
// integer-channel counterpart of `GatherReturnsFourTexelsInGatherOrder`
// above, same 2x2-image/component-1-selection proof technique, but
// through an `R32G32B32A32_SINT`-formatted image and a `<4 x i32>`-
// shaped `out`.
TEST_F(ImageSamplingTest, Gather2DI32ReturnsFourTexelsInGatherOrder) {
  int32_t Storage[2][2][4] = {{{9, 4, 0, 0}, {9, 6, 0, 0}},
                              {{9, 3, 0, 0}, {9, 7, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 2, 2, ResourceFormat::R32G32B32A32_SINT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherI32Fn Fn = resolve<GatherI32Fn>(
      addWrapper("gather_i32", "feme.cpu.image.gather.2d.v4i32"));
  int32_t Out[4] = {-1, -1, -1, -1};
  // Component 1 (green): T(X0,Y0)=4, T(X1,Y0)=6, T(X0,Y1)=3, T(X1,Y1)=7.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*Component=*/1, 0, 0,
     true, Out);
  EXPECT_EQ(Out[0], 3); // T(X0,Y1)
  EXPECT_EQ(Out[1], 7); // T(X1,Y1)
  EXPECT_EQ(Out[2], 6); // T(X1,Y0)
  EXPECT_EQ(Out[3], 4); // T(X0,Y0)
}

// (Roadmap L125(q)) Same "integer border default must be swizzled" gap
// as `SampleI32AppliesImageViewSwizzleToBorderColorFallback` above, but
// for `Gather*` -- confirmed via a real CTS regression
// (`sampler.border_swizzle.r16_sint.barg.transparent_black.gather_3.*`)
// where the whole 2x2 gather footprint landed on the border. Every one
// of the 4 gathered corners must go through the same swizzle an
// in-bounds tap would. Uses `VK_BORDER_COLOR_INT_OPAQUE_BLACK` for the
// same reason `SampleI32AppliesImageViewSwizzleToBorderColorFallback`
// above does (see that test's own updated comment).
TEST_F(ImageSamplingTest, Gather2DI32AppliesImageViewSwizzleToBorderColorFallback) {
  int32_t Storage[1][1][4] = {{{9, 9, 9, 9}}}; // Never read: always border.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32G32B32A32_SINT,
      Layout);
  Img.Swizzle = packImageSwizzle(
      ImageComponentSwizzle::B, ImageComponentSwizzle::A,
      ImageComponentSwizzle::R, ImageComponentSwizzle::Zero);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp = makeSampler(SamplerFilter::Linear,
                                           SamplerAddressMode::ClampToBorder);
  Samp.BorderColor[3] = 1.0f; // VK_BORDER_COLOR_INT_OPAQUE_BLACK.
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherI32Fn Fn = resolve<GatherI32Fn>(
      addWrapper("gather_i32_border", "feme.cpu.image.gather.2d.v4i32"));
  int32_t Out[4] = {-1, -1, -1, -1};
  // Component 1 (green): post-swizzle border is {0, 1, 0, 0} (G <- A,
  // pre-swizzle border A is 1), so every one of the 4 gathered corners
  // (all on the border) must read 1, not the pre-swizzle green (0).
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 5.0f, 5.0f, /*Component=*/1, 0, 0,
     true, Out);
  EXPECT_EQ(Out[0], 1);
  EXPECT_EQ(Out[1], 1);
  EXPECT_EQ(Out[2], 1);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, GatherCmpArray2DIsolatesNamedLayer) {
  // Roadmap H124q: `feme.cpu.image.gathercmp.array2d.v4f32` must gather
  // its 2x2 depth-comparison footprint from the requested `ArrayLayer`
  // only, never blending or mixing in a different layer's own texels --
  // mirroring `Sample2DArrayReadsRequestedLayer`'s own identical "isolate
  // the layer" proof technique, layered on top of
  // `GatherCmpReturnsFourTexelsInDrefGatherOrder`'s own per-corner
  // pass/fail comparison setup. Layer 0's depth values are the same ones
  // `GatherCmpReturnsFourTexelsInDrefGatherOrder` uses; layer 1's are
  // deliberately the *opposite* pass/fail pattern, so reading the wrong
  // layer flips every corner's own result.
  float Storage[2][2][2][4] = {
      {{{0.4f, 0, 0, 0}, {0.6f, 0, 0, 0}}, {{0.3f, 0, 0, 0}, {0.7f, 0, 0, 0}}},
      {{{0.9f, 0, 0, 0}, {0.1f, 0, 0, 0}}, {{0.8f, 0, 0, 0}, {0.2f, 0, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2DArray(Storage, sizeof(Storage), 2, 2, 2,
                                             ResourceFormat::R32G32B32A32_FLOAT,
                                             Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCmpArray2DFn Fn = resolve<GatherCmpArray2DFn>(addWrapper(
      "gathercmp_array2d", "feme.cpu.image.gathercmp.array2d.v4f32"));
  // Layer 0: Ref (0.5) <= Texel: T(X0,Y0)=0.4 fails, T(X1,Y0)=0.6 passes,
  // T(X0,Y1)=0.3 fails, T(X1,Y1)=0.7 passes.
  float Layer0[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/0.0f, 0.5f,
     0, 0, true, Layer0);
  EXPECT_FLOAT_EQ(Layer0[0], 0.0f); // C(X0,Y1)
  EXPECT_FLOAT_EQ(Layer0[1], 1.0f); // C(X1,Y1)
  EXPECT_FLOAT_EQ(Layer0[2], 1.0f); // C(X1,Y0)
  EXPECT_FLOAT_EQ(Layer0[3], 0.0f); // C(X0,Y0)
  // Layer 1: Ref (0.5) <= Texel: T(X0,Y0)=0.9 passes, T(X1,Y0)=0.1
  // fails, T(X0,Y1)=0.8 passes, T(X1,Y1)=0.2 fails -- every corner
  // flipped relative to layer 0.
  float Layer1[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/1.0f, 0.5f,
     0, 0, true, Layer1);
  EXPECT_FLOAT_EQ(Layer1[0], 1.0f); // C(X0,Y1)
  EXPECT_FLOAT_EQ(Layer1[1], 0.0f); // C(X1,Y1)
  EXPECT_FLOAT_EQ(Layer1[2], 0.0f); // C(X1,Y0)
  EXPECT_FLOAT_EQ(Layer1[3], 1.0f); // C(X0,Y0)
}

TEST_F(ImageSamplingTest, GatherArray2DIsolatesNamedLayer) {
  // Roadmap H124q: the non-comparison `Gather` counterpart of
  // `GatherCmpArray2DIsolatesNamedLayer` above -- same two-layer setup
  // and same "every corner differs between layers" proof technique, but
  // gathering a plain green-channel `Component` rather than comparing
  // against a `Dref`.
  float Storage[2][2][2][4] = {{{{9.0f, 0.4f, 0, 0}, {9.0f, 0.6f, 0, 0}},
                                {{9.0f, 0.3f, 0, 0}, {9.0f, 0.7f, 0, 0}}},
                               {{{9.0f, 0.1f, 0, 0}, {9.0f, 0.9f, 0, 0}},
                                {{9.0f, 0.2f, 0, 0}, {9.0f, 0.8f, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 2,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherArray2DFn Fn = resolve<GatherArray2DFn>(
      addWrapper("gather_array2d", "feme.cpu.image.gather.array2d.v4f32"));
  // Layer 0, component 1 (green): T(X0,Y0)=0.4, T(X1,Y0)=0.6,
  // T(X0,Y1)=0.3, T(X1,Y1)=0.7.
  float Layer0[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/0.0f,
     /*Component=*/1, 0, 0, true, Layer0);
  EXPECT_FLOAT_EQ(Layer0[0], 0.3f); // T(X0,Y1)
  EXPECT_FLOAT_EQ(Layer0[1], 0.7f); // T(X1,Y1)
  EXPECT_FLOAT_EQ(Layer0[2], 0.6f); // T(X1,Y0)
  EXPECT_FLOAT_EQ(Layer0[3], 0.4f); // T(X0,Y0)
  // Layer 1, component 1 (green): T(X0,Y0)=0.1, T(X1,Y0)=0.9,
  // T(X0,Y1)=0.2, T(X1,Y1)=0.8 -- every corner differs from layer 0.
  float Layer1[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/1.0f,
     /*Component=*/1, 0, 0, true, Layer1);
  EXPECT_FLOAT_EQ(Layer1[0], 0.2f); // T(X0,Y1)
  EXPECT_FLOAT_EQ(Layer1[1], 0.8f); // T(X1,Y1)
  EXPECT_FLOAT_EQ(Layer1[2], 0.9f); // T(X1,Y0)
  EXPECT_FLOAT_EQ(Layer1[3], 0.1f); // T(X0,Y0)
}

// (Roadmap L125(k)) `feme.cpu.image.gather.array2d.v4i32` -- the
// integer-channel counterpart of `GatherArray2DIsolatesNamedLayer`
// above, same two-layer/"every corner differs" proof technique, through
// an `R32G32B32A32_SINT`-formatted image and a `<4 x i32>`-shaped `out`.
TEST_F(ImageSamplingTest, GatherArray2DI32IsolatesNamedLayer) {
  int32_t Storage[2][2][2][4] = {{{{9, 4, 0, 0}, {9, 6, 0, 0}},
                                  {{9, 3, 0, 0}, {9, 7, 0, 0}}},
                                 {{{9, 1, 0, 0}, {9, 9, 0, 0}},
                                  {{9, 2, 0, 0}, {9, 8, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 2,
                       ResourceFormat::R32G32B32A32_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherArray2DI32Fn Fn = resolve<GatherArray2DI32Fn>(addWrapper(
      "gather_array2d_i32", "feme.cpu.image.gather.array2d.v4i32"));
  // Layer 0, component 1 (green): T(X0,Y0)=4, T(X1,Y0)=6, T(X0,Y1)=3,
  // T(X1,Y1)=7.
  int32_t Layer0[4] = {-1, -1, -1, -1};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/0.0f,
     /*Component=*/1, 0, 0, true, Layer0);
  EXPECT_EQ(Layer0[0], 3); // T(X0,Y1)
  EXPECT_EQ(Layer0[1], 7); // T(X1,Y1)
  EXPECT_EQ(Layer0[2], 6); // T(X1,Y0)
  EXPECT_EQ(Layer0[3], 4); // T(X0,Y0)
  // Layer 1, component 1 (green): T(X0,Y0)=1, T(X1,Y0)=9, T(X0,Y1)=2,
  // T(X1,Y1)=8 -- every corner differs from layer 0.
  int32_t Layer1[4] = {-1, -1, -1, -1};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/1.0f,
     /*Component=*/1, 0, 0, true, Layer1);
  EXPECT_EQ(Layer1[0], 2); // T(X0,Y1)
  EXPECT_EQ(Layer1[1], 8); // T(X1,Y1)
  EXPECT_EQ(Layer1[2], 9); // T(X1,Y0)
  EXPECT_EQ(Layer1[3], 1); // T(X0,Y0)
}

TEST_F(ImageSamplingTest, GatherCmpCubeIsolatesNamedFace) {
  // Roadmap H124r: `feme.cpu.image.gathercmp.cube.v4f32` must gather its
  // 2x2 depth-comparison footprint from the requested cube face only,
  // never blending or mixing in a different face's own texels --
  // mirroring `GatherCmpArray2DIsolatesNamedLayer`'s own identical
  // "isolate the layer" proof technique, since a cube is purely a
  // view-level addressing convention over an ordinary 2D-array-shaped
  // image (`femeRTSelectCubeFace`'s own layer-address convention). A
  // pure `(+X, 0, 0)` direction selects face 0 at its own center `(0.5,
  // 0.5)`; a pure `(-X, 0, 0)` direction selects face 1, also at its own
  // center -- both land exactly on the shared corner of all four texels,
  // mirroring `GatherCmpArray2DIsolatesNamedLayer`'s own `(0.5, 0.5)`
  // coordinate. Face 1's depth values are deliberately the *opposite*
  // pass/fail pattern from face 0's, so reading the wrong face flips
  // every corner's own result.
  float Storage[6][2][2][4] = {
      {{{0.4f, 0, 0, 0}, {0.6f, 0, 0, 0}}, {{0.3f, 0, 0, 0}, {0.7f, 0, 0, 0}}},
      {{{0.9f, 0, 0, 0}, {0.1f, 0, 0, 0}}, {{0.8f, 0, 0, 0}, {0.2f, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                                             ResourceFormat::R32G32B32A32_FLOAT,
                                             Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCmpCubeFn Fn = resolve<GatherCmpCubeFn>(
      addWrapper("gathercmp_cube", "feme.cpu.image.gathercmp.cube.v4f32"));
  // Face 0 (+X direction): Ref (0.5) <= Texel: T(X0,Y0)=0.4 fails,
  // T(X1,Y0)=0.6 passes, T(X0,Y1)=0.3 fails, T(X1,Y1)=0.7 passes.
  float Face0[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*Dref=*/0.5f, true, Face0);
  EXPECT_FLOAT_EQ(Face0[0], 0.0f); // C(X0,Y1)
  EXPECT_FLOAT_EQ(Face0[1], 1.0f); // C(X1,Y1)
  EXPECT_FLOAT_EQ(Face0[2], 1.0f); // C(X1,Y0)
  EXPECT_FLOAT_EQ(Face0[3], 0.0f); // C(X0,Y0)
  // Face 1 (-X direction): Ref (0.5) <= Texel: T(X0,Y0)=0.9 passes,
  // T(X1,Y0)=0.1 fails, T(X0,Y1)=0.8 passes, T(X1,Y1)=0.2 fails --
  // every corner flipped relative to face 0.
  float Face1[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/-1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*Dref=*/0.5f, true, Face1);
  EXPECT_FLOAT_EQ(Face1[0], 1.0f); // C(X0,Y1)
  EXPECT_FLOAT_EQ(Face1[1], 0.0f); // C(X1,Y1)
  EXPECT_FLOAT_EQ(Face1[2], 0.0f); // C(X1,Y0)
  EXPECT_FLOAT_EQ(Face1[3], 1.0f); // C(X0,Y0)
}

TEST_F(ImageSamplingTest, GatherCubeIsolatesNamedFace) {
  // Roadmap H124r: the non-comparison `Gather` counterpart of
  // `GatherCmpCubeIsolatesNamedFace` above -- same two-face setup and
  // same "every corner differs between faces" proof technique, but
  // gathering a plain green-channel `Component` rather than comparing
  // against a `Dref`.
  float Storage[6][2][2][4] = {
      {{{9.0f, 0.4f, 0, 0}, {9.0f, 0.6f, 0, 0}},
       {{9.0f, 0.3f, 0, 0}, {9.0f, 0.7f, 0, 0}}},
      {{{9.0f, 0.1f, 0, 0}, {9.0f, 0.9f, 0, 0}},
       {{9.0f, 0.2f, 0, 0}, {9.0f, 0.8f, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCubeFn Fn = resolve<GatherCubeFn>(
      addWrapper("gather_cube", "feme.cpu.image.gather.cube.v4f32"));
  // Face 0 (+X direction), component 1 (green): T(X0,Y0)=0.4,
  // T(X1,Y0)=0.6, T(X0,Y1)=0.3, T(X1,Y1)=0.7.
  float Face0[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*Component=*/1, true, Face0);
  EXPECT_FLOAT_EQ(Face0[0], 0.3f); // T(X0,Y1)
  EXPECT_FLOAT_EQ(Face0[1], 0.7f); // T(X1,Y1)
  EXPECT_FLOAT_EQ(Face0[2], 0.6f); // T(X1,Y0)
  EXPECT_FLOAT_EQ(Face0[3], 0.4f); // T(X0,Y0)
  // Face 1 (-X direction), component 1 (green): T(X0,Y0)=0.1,
  // T(X1,Y0)=0.9, T(X0,Y1)=0.2, T(X1,Y1)=0.8 -- every corner differs
  // from face 0.
  float Face1[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/-1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*Component=*/1, true, Face1);
  EXPECT_FLOAT_EQ(Face1[0], 0.2f); // T(X0,Y1)
  EXPECT_FLOAT_EQ(Face1[1], 0.8f); // T(X1,Y1)
  EXPECT_FLOAT_EQ(Face1[2], 0.9f); // T(X1,Y0)
  EXPECT_FLOAT_EQ(Face1[3], 0.1f); // T(X0,Y0)
}

// (Roadmap L125(k)) `feme.cpu.image.gather.cube.v4i32` -- the
// integer-channel counterpart of `GatherCubeIsolatesNamedFace` above,
// same two-face/"every corner differs" proof technique, through an
// `R32G32B32A32_SINT`-formatted image and a `<4 x i32>`-shaped `out`.
TEST_F(ImageSamplingTest, GatherCubeI32IsolatesNamedFace) {
  int32_t Storage[6][2][2][4] = {
      {{{9, 4, 0, 0}, {9, 6, 0, 0}}, {{9, 3, 0, 0}, {9, 7, 0, 0}}},
      {{{9, 1, 0, 0}, {9, 9, 0, 0}}, {{9, 2, 0, 0}, {9, 8, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                       ResourceFormat::R32G32B32A32_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCubeI32Fn Fn = resolve<GatherCubeI32Fn>(
      addWrapper("gather_cube_i32", "feme.cpu.image.gather.cube.v4i32"));
  // Face 0 (+X direction), component 1 (green): T(X0,Y0)=4, T(X1,Y0)=6,
  // T(X0,Y1)=3, T(X1,Y1)=7.
  int32_t Face0[4] = {-1, -1, -1, -1};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*Component=*/1, true, Face0);
  EXPECT_EQ(Face0[0], 3); // T(X0,Y1)
  EXPECT_EQ(Face0[1], 7); // T(X1,Y1)
  EXPECT_EQ(Face0[2], 6); // T(X1,Y0)
  EXPECT_EQ(Face0[3], 4); // T(X0,Y0)
  // Face 1 (-X direction), component 1 (green): T(X0,Y0)=1, T(X1,Y0)=9,
  // T(X0,Y1)=2, T(X1,Y1)=8 -- every corner differs from face 0.
  int32_t Face1[4] = {-1, -1, -1, -1};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/-1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*Component=*/1, true, Face1);
  EXPECT_EQ(Face1[0], 2); // T(X0,Y1)
  EXPECT_EQ(Face1[1], 8); // T(X1,Y1)
  EXPECT_EQ(Face1[2], 9); // T(X1,Y0)
  EXPECT_EQ(Face1[3], 1); // T(X0,Y0)
}

// Roadmap L125(j): `TextureCube::Gather`'s own 2x2 footprint must remap
// across a cube face edge the same way a `LINEAR` `Sample` already does
// (`SampleCubeSeamlessBlendsAcrossFaceEdge` above), not clamp in place --
// reusing that same test's exact direction vector and two-face layout
// (face 0 uniformly 100.0, face 4 uniformly 0.0, `(DirX=1, DirY=0,
// DirZ=0.6)` placing the footprint's low-`U` tap one texel below `u==0`,
// remapping onto face 4). Pre-L125(j), `femeRTComputeBilinearSupport`'s
// own forced `ClampToEdge` addressing would have clamped every tap back
// onto face 0, reading `100.0` in every one of the four gathered
// corners; this test requires two of the four (`T00`/`T01`, the two
// low-`U` taps) to instead read face 4's own `0.0`.
TEST_F(ImageSamplingTest, GatherCubeSeamlessBlendsAcrossFaceEdge) {
  float Storage[6][2][2][4];
  for (unsigned Face = 0; Face < 6; ++Face)
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Storage[Face][Y][X][C] = (Face == 0) ? 100.0f : 0.0f;
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCubeFn Fn = resolve<GatherCubeFn>(addWrapper(
      "gather_cube_seamless", "feme.cpu.image.gather.cube.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.6f, /*Component=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);   // T(X0,Y1) remapped onto face 4.
  EXPECT_FLOAT_EQ(Out[1], 100.0f); // T(X1,Y1), still face 0.
  EXPECT_FLOAT_EQ(Out[2], 100.0f); // T(X1,Y0), still face 0.
  EXPECT_FLOAT_EQ(Out[3], 0.0f);   // T(X0,Y0) remapped onto face 4.
}

// (Roadmap L125(k)) `feme.cpu.image.gather.cube.v4i32` -- the
// integer-channel counterpart of `GatherCubeSeamlessBlendsAcrossFaceEdge`
// above, confirming `femeRTFetchCubeSeamlessTexelI32` performs the same
// cross-face remap as its float sibling, through an
// `R32G32B32A32_SINT`-formatted image and a `<4 x i32>`-shaped `out`.
TEST_F(ImageSamplingTest, GatherCubeI32SeamlessBlendsAcrossFaceEdge) {
  int32_t Storage[6][2][2][4];
  for (unsigned Face = 0; Face < 6; ++Face)
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Storage[Face][Y][X][C] = (Face == 0) ? 100 : 0;
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                       ResourceFormat::R32G32B32A32_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCubeI32Fn Fn = resolve<GatherCubeI32Fn>(addWrapper(
      "gather_cube_i32_seamless", "feme.cpu.image.gather.cube.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.6f, /*Component=*/0, true, Out);
  EXPECT_EQ(Out[0], 0);   // T(X0,Y1) remapped onto face 4.
  EXPECT_EQ(Out[1], 100); // T(X1,Y1), still face 0.
  EXPECT_EQ(Out[2], 100); // T(X1,Y0), still face 0.
  EXPECT_EQ(Out[3], 0);   // T(X0,Y0) remapped onto face 4.
}


// The depth-comparison counterpart of `GatherCubeSeamlessBlendsAcrossFace
// Edge` above, mirroring `SampleCmpCubeSeamlessBlendsAcrossFaceEdge`'s
// own relationship to `SampleCubeSeamlessBlendsAcrossFaceEdge`: face 0
// uniformly a depth of `1.0`, face 4 uniformly `0.0`, `Dref=0.5` with
// `LessEqual` passes (`1.0`) on face 0's own texels but fails (`0.0`) on
// face 4's -- so the same two low-`U` corners that
// `GatherCubeSeamlessBlendsAcrossFaceEdge` observes reading face 4's
// color must here instead observe a failed comparison.
TEST_F(ImageSamplingTest, GatherCmpCubeSeamlessBlendsAcrossFaceEdge) {
  float Storage[6][2][2][4];
  for (unsigned Face = 0; Face < 6; ++Face)
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Storage[Face][Y][X][C] = (Face == 0) ? 1.0f : 0.0f;
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                                             ResourceFormat::R32G32B32A32_FLOAT,
                                             Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  GatherCmpCubeFn Fn = resolve<GatherCmpCubeFn>(addWrapper(
      "gathercmp_cube_seamless", "feme.cpu.image.gathercmp.cube.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.6f, /*Dref=*/0.5f, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f); // T(X0,Y1) remapped onto face 4, fails.
  EXPECT_FLOAT_EQ(Out[1], 1.0f); // T(X1,Y1), still face 0, passes.
  EXPECT_FLOAT_EQ(Out[2], 1.0f); // T(X1,Y0), still face 0, passes.
  EXPECT_FLOAT_EQ(Out[3], 0.0f); // T(X0,Y0) remapped onto face 4, fails.
}

TEST_F(ImageSamplingTest, ExplicitLoadFetchesExactTexel) {
  float Storage[2][2][4] = {{{1, 2, 3, 4}, {5, 6, 7, 8}},
                            {{9, 10, 11, 12}, {13, 14, 15, 16}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};

  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 1, 1, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 13.0f);
  EXPECT_FLOAT_EQ(Out[1], 14.0f);
  EXPECT_FLOAT_EQ(Out[2], 15.0f);
  EXPECT_FLOAT_EQ(Out[3], 16.0f);
}

// Roadmap E25: the CPU runtime's typed sample table broadened beyond its
// original three formats -- each of these exercises one newly-decoded
// format through `feme.cpu.image.load.2d.v4f32` (an exact, unfiltered
// fetch, so the expected values are the format's own decode, not a
// filtered blend).

TEST_F(ImageSamplingTest, LoadFetchesPartialComponentFloatFormats) {
  // A missing color component reads 0.0 and a missing alpha reads 1.0,
  // matching `OpImageFetch`'s own convention for a partial-component
  // format.
  float R32Storage[1][1] = {{7.0f}};
  FemeImageSubresourceLayout R32Layout;
  FemeImageDescriptor R32Img = makeImage2D(
      R32Storage, sizeof(R32Storage), 1, 1, ResourceFormat::R32_FLOAT, R32Layout);
  FemeImageDescriptor R32Heap[1] = {R32Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(R32Heap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 7.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);

  float RG32Storage[1][1][2] = {{{3.0f, 4.0f}}};
  FemeImageSubresourceLayout RG32Layout;
  FemeImageDescriptor RG32Img =
      makeImage2D(RG32Storage, sizeof(RG32Storage), 1, 1,
                  ResourceFormat::R32G32_FLOAT, RG32Layout);
  FemeImageDescriptor RG32Heap[1] = {RG32Img};
  Fn(RG32Heap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 3.0f);
  EXPECT_FLOAT_EQ(Out[1], 4.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);

  float RGB32Storage[1][1][3] = {{{1.0f, 2.0f, 3.0f}}};
  FemeImageSubresourceLayout RGB32Layout;
  FemeImageDescriptor RGB32Img =
      makeImage2D(RGB32Storage, sizeof(RGB32Storage), 1, 1,
                  ResourceFormat::R32G32B32_FLOAT, RGB32Layout);
  FemeImageDescriptor RGB32Heap[1] = {RGB32Img};
  Fn(RGB32Heap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 2.0f);
  EXPECT_FLOAT_EQ(Out[2], 3.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR8G8B8A8Snorm) {
  // Little-endian bytes -127, 0, 127, -128 -> R=-1.0, G=0.0, B=1.0,
  // A=-1.0 (clamped, per the Vulkan SNORM conversion this format's typed-
  // buffer helper already implements).
  uint8_t Bytes[4] = {(uint8_t)-127, 0, 127, (uint8_t)-128};
  uint32_t Storage[1][1];
  memcpy(Storage, Bytes, sizeof(Bytes));
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R8G8B8A8_SNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], -1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 1.0f);
  EXPECT_FLOAT_EQ(Out[3], -1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesB8G8R8A8Unorm) {
  // Memory order B, G, R, A -- must read back swizzled to logical R, G, B,
  // A, the same swizzle `feme::graphics::unpackColor` applies for this
  // format's render-target path. Little-endian bytes are B=0xFF, G=0x00,
  // R=0x00, A=0xFF, so logical R=0, G=0, B=1.0, A=1.0.
  uint32_t Storage[1][1] = {{0xFF0000FFu}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::B8G8R8A8_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f); // R
  EXPECT_FLOAT_EQ(Out[1], 0.0f); // G
  EXPECT_FLOAT_EQ(Out[2], 1.0f); // B
  EXPECT_FLOAT_EQ(Out[3], 1.0f); // A
}

// (Roadmap H8r) `B8G8R8A8_UNORM_SRGB`, an entirely unmapped format H8g's
// own audit split off. Combines the `SRGBDecodeOnSample` sRGB-decode
// behavior above with `LoadFetchesB8G8R8A8Unorm`'s own B/G/R/A memory
// swizzle: a single texel with R=G=B=188/255 (~0.7372549, the sRGB
// encoding of linear 0.5) and A=255 (1.0, never sRGB-decoded). Since
// R=G=B here, the same little-endian word works for both memory orders.
TEST_F(ImageSamplingTest, SRGBDecodeOnSampleBGRA8) {
  uint32_t Storage[1][1] = {{0xFFBCBCBCu}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::B8G8R8A8_UNORM_SRGB, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_NEAR(Out[0], 0.5f, 0.01f);
  EXPECT_NEAR(Out[1], 0.5f, 0.01f);
  EXPECT_NEAR(Out[2], 0.5f, 0.01f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f); // Alpha is never sRGB-decoded.
}

TEST_F(ImageSamplingTest, LoadFetchesR10G10B10A2Unorm) {
  // From the MSB down: 2 bits A, 10 bits B, 10 bits G, 10 bits R. Set
  // R=1023 (max), G=0, B=0, A=3 (max) so each field is unambiguous.
  uint32_t Storage[1][1] = {{(3u << 30) | 1023u}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R10G10B10A2_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR10G10B10A2Snorm) {
  // Roadmap H19o: the signed-normalized sibling of
  // `LoadFetchesR10G10B10A2Unorm` above, same MSB-down bit layout but
  // each field a signed fixed-point value. R = 0x1FF (511, the maximum
  // positive 10-bit signed value) decodes to 1.0; G = 0x200 (-512, the
  // most negative 10-bit signed value) decodes to -512/511, which clamps
  // to -1.0 per the Vulkan spec's own SNORM conversion; B = 0 decodes to
  // 0.0; A = 1 (the maximum positive 2-bit signed value) decodes to 1.0.
  uint32_t Storage[1][1] = {{(1u << 30) | (0x200u << 10) | 0x1FFu}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R10G10B10A2_SNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], -1.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR11G11B10Float) {
  // An all-zero-bits texel decodes to (0, 0, 0, 1.0): every field's
  // exponent and mantissa are zero, i.e. positive zero, and this format
  // carries no alpha channel (always reads 1.0).
  uint32_t Storage[1][1] = {{0}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R11G11B10_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR11G11B10FloatNonZeroValues) {
  // Roadmap H18 regression coverage: an all-zero-bits texel (the only
  // case the pre-existing `LoadFetchesR11G11B10Float` test above covers)
  // cannot distinguish a correct decode from the real bug this row fixed
  // (`femeRTUnpackR11G11B10Float`'s field-to-binary16 shift was one bit
  // too many, `0` shifted by any amount is still `0`). Use a non-zero,
  // distinct value for each channel instead: R=1.0 (11-bit field, 6-bit
  // mantissa: exponent 15, mantissa 0), G=2.0 (exponent 16, mantissa 0),
  // B=1.5 (10-bit field, 5-bit mantissa: exponent 15, mantissa 16 --
  // 1 + 16/32 = 1.5). Packed from the LSB up: R (bits 0-10), G (bits
  // 11-21), B (bits 22-31).
  uint32_t RRaw = (15u << 6) | 0u;
  uint32_t GRaw = (16u << 6) | 0u;
  uint32_t BRaw = (15u << 5) | 16u;
  uint32_t Storage[1][1] = {{RRaw | (GRaw << 11) | (BRaw << 22)}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R11G11B10_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 2.0f);
  EXPECT_FLOAT_EQ(Out[2], 1.5f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesE5B9G9R9Ufloat) {
  // (Roadmap H8q) An all-zero-bits texel decodes to (0, 0, 0, 1.0): a
  // zero shared exponent and zero mantissas both decode to zero for
  // every channel, and this format carries no alpha channel (always
  // reads 1.0).
  uint32_t Storage[1][1] = {{0}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::E5B9G9R9_UFLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesE5B9G9R9UfloatNonZeroValues) {
  // A non-zero-bits regression case, the same rationale
  // `LoadFetchesR11G11B10FloatNonZeroValues` above documents (an
  // all-zero-bits texel alone cannot distinguish a correct decode from a
  // decode that is broken in a way that only manifests for a non-zero
  // input). Each channel's value is `mantissa * 2^(exponent - 15 - 9)`;
  // with a shared exponent of 24 (so `exponent - 24 == 0`), R = mantissa
  // 256 = 256.0, G = mantissa 128 = 128.0, B = mantissa 64 = 64.0.
  // Packed from the LSB up: R (bits 0-8), G (bits 9-17), B (bits 18-26),
  // exponent (bits 27-31).
  uint32_t Word = 256u | (128u << 9) | (64u << 18) | (24u << 27);
  uint32_t Storage[1][1] = {{Word}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::E5B9G9R9_UFLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 256.0f);
  EXPECT_FLOAT_EQ(Out[1], 128.0f);
  EXPECT_FLOAT_EQ(Out[2], 64.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR16G16B16A16Float) {
  // binary16 1.0 is 0x3C00; -2.0 is 0xC000.
  uint16_t Storage[1][1][4] = {{{0x3C00, 0x0000, 0xC000, 0x3C00}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R16G16B16A16_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], -2.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR16G16B16A16Unorm) {
  // Roadmap H19h: 65535/0/32768/0 normalize to 1.0/0.0/~0.5/0.0.
  uint16_t Storage[1][1][4] = {{{65535, 0, 32768, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R16G16B16A16_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_NEAR(Out[2], 0.5f, 0.001f);
  EXPECT_FLOAT_EQ(Out[3], 0.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR16G16B16A16Snorm) {
  // Roadmap H19h: 32767/-32767/-32768/0 normalize to 1.0/-1.0/-1.0/0.0
  // (`-32768` clamps to `-1.0`, mirroring the 8-bit SNORM formats' own
  // `-128` clamp).
  uint16_t Storage[1][1][4] = {{{(uint16_t)32767, (uint16_t)-32767,
                                 (uint16_t)-32768, (uint16_t)0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R16G16B16A16_SNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], -1.0f);
  EXPECT_FLOAT_EQ(Out[2], -1.0f);
  EXPECT_FLOAT_EQ(Out[3], 0.0f);
}

// Roadmap H19j: `R8_{UNORM,SNORM,UINT,SINT}`, the single-channel
// mandatory `shaderStorageImageExtendedFormats` formats -- the missing
// G/B components read `0.0`/`0`, alpha reads `1.0`/`1`, matching the
// existing partial-component convention.

TEST_F(ImageSamplingTest, LoadFetchesR8Unorm) {
  uint8_t Storage[1][1] = {{255}}; // 255/255 = 1.0.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR8Snorm) {
  int8_t Storage[1][1] = {{-127}}; // -127/127 = -1.0.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_SNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], -1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR8G8Unorm) {
  uint8_t Storage[1][1][2] = {{{255, 128}}}; // 255/255=1.0, 128/255~=0.502.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8G8_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_NEAR(Out[1], 128.0f / 255.0f, 1e-6f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR8G8Snorm) {
  int8_t Storage[1][1][2] = {{{-127, 64}}}; // -127/127=-1.0, 64/127~=0.504.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8G8_SNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], -1.0f);
  EXPECT_NEAR(Out[1], 64.0f / 127.0f, 1e-6f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

// Roadmap H19n: `R16_{FLOAT,UNORM,SNORM,UINT,SINT}`, the single-channel
// mandatory `shaderStorageImageExtendedFormats` formats -- same
// partial-component convention as the `R8`/`R8G8` formats above.

TEST_F(ImageSamplingTest, LoadFetchesR16Float) {
  // binary16 1.0 is 0x3C00, matching R16G16B16A16_FLOAT's own precedent.
  uint16_t Storage[1][1] = {{0x3C00}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR16Unorm) {
  uint16_t Storage[1][1] = {{65535}}; // 65535/65535 = 1.0.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR16Snorm) {
  uint16_t Storage[1][1] = {{(uint16_t)32767}}; // 32767/32767 = 1.0.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_SNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

// Roadmap H19n: `R16G16_{FLOAT,UNORM,SNORM,UINT,SINT}`, the two-channel
// mandatory `shaderStorageImageExtendedFormats` formats -- same
// partial-component convention as the single-channel `R16` formats
// above.

TEST_F(ImageSamplingTest, LoadFetchesR16G16Float) {
  // binary16 1.0 is 0x3C00; -2.0 is 0xC000, matching
  // R16G16B16A16_FLOAT's own precedent.
  uint16_t Storage[1][1][2] = {{{0x3C00, 0xC000}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], -2.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR16G16Unorm) {
  // 65535/0 normalize to 1.0/0.0.
  uint16_t Storage[1][1][2] = {{{65535, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesR16G16Snorm) {
  // 32767/-32767 normalize to 1.0/-1.0.
  uint16_t Storage[1][1][2] = {{{(uint16_t)32767, (uint16_t)-32767}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_SNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], -1.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesA8Unorm) {
  uint8_t Storage[1][1] = {{128}}; // ~0.502.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::A8_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_NEAR(Out[3], 128.0f / 255.0f, 1e-6f);
}

TEST_F(ImageSamplingTest, LoadFetchesA1B5G5R5Unorm) {
  // From the MSB down: 1 bit A, 5 bits B, 5 bits G, 5 bits R. Set R=31
  // (max), G=0, B=0, A=1 (set) so each field is unambiguous.
  uint16_t Storage[1][1] = {{(uint16_t)((1u << 15) | 31u)}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::A1B5G5R5_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

// Roadmap H8e: `B4G4R4A4_UNORM`/`A1R5G5B5_UNORM`, a CTS-confirmed genuine
// `SAMPLED_IMAGE_BIT` gap (rather than a reporting-only one) for two more
// of roadmap H7r's own packed 16-bit formats.

TEST_F(ImageSamplingTest, LoadFetchesB4G4R4A4Unorm) {
  // From the MSB down: 4 bits B, 4 bits G, 4 bits R, 4 bits A. Set R=15
  // (max), G=0, B=0, A=15 (max) so each field is unambiguous.
  uint16_t Storage[1][1] = {{(uint16_t)((15u << 4) | 15u)}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::B4G4R4A4_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesA1R5G5B5Unorm) {
  // From the MSB down: 1 bit A, 5 bits R, 5 bits G, 5 bits B. Set R=31
  // (max), G=0, B=0, A=1 (set) so each field is unambiguous.
  uint16_t Storage[1][1] = {{(uint16_t)((1u << 15) | (31u << 10))}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::A1R5G5B5_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

// Roadmap H8g: `R5G6B5_UNORM`/`B5G6R5_UNORM`, another CTS-confirmed
// genuine `SAMPLED_IMAGE_BIT` gap for the last two of roadmap H7r's own
// packed 16-bit formats -- neither has an alpha channel, unlike every
// format above, so alpha should read back as an implicit, unwritable
// `1.0`.

TEST_F(ImageSamplingTest, LoadFetchesR5G6B5Unorm) {
  // From the MSB down: 5 bits R, 6 bits G, 5 bits B. Set R=31 (max), G=0,
  // B=0 so each field is unambiguous.
  uint16_t Storage[1][1] = {{(uint16_t)(31u << 11)}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R5G6B5_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesB5G6R5Unorm) {
  // From the MSB down: 5 bits B, 6 bits G, 5 bits R. Set R=31 (max, the
  // last 5 bits here), G=0, B=0 so each field is unambiguous.
  uint16_t Storage[1][1] = {{(uint16_t)31u}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::B5G6R5_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

// Roadmap F8b: the single-component depth/stencil formats
// `feme::vulkan::buildSubpassInputHeap` feeds a depth/stencil subpass
// input attachment through -- these were the format-decode gap that left
// every such `subpassLoad` reading zero (see FeMeRuntimeCPU.c's own
// roadmap F8b comment).

TEST_F(ImageSamplingTest, LoadFetchesD16Unorm) {
  uint16_t Storage[1][1] = {{32768}}; // 32768 / 65535 ~= 0.5000076.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::D16_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_NEAR(Out[0], 32768.0f / 65535.0f, 1e-6f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesD32Float) {
  // The identity case, like R32_FLOAT: no conversion, just an unread
  // color/alpha component pad.
  float Storage[1][1] = {{0.25f}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::D32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.25f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

TEST_F(ImageSamplingTest, LoadFetchesS8Uint) {
  // A stencil reference value, normalized to `[0.0, 1.0]` (matching
  // `A8_UNORM`'s own normalized-component convention): 64 / 255 ~= 0.251.
  uint8_t Storage[1][1] = {{64}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::S8_UINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_NEAR(Out[0], 64.0f / 255.0f, 1e-6f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

// Roadmap F8b: a multisampled image (`SampleCount > 1`) packs every
// sample of one texel contiguously; `femeRTFetchTexel2D`'s addressing
// must skip `SampleCount` samples' worth of bytes per texel step along a
// row, not one, or texel (1, 0) below would alias sample 1 of texel
// (0, 0). Sample 0 of each texel is the default `femeCpuImageLoad2DV4F32`
// reads when a caller passes a constant `0` `Sample` argument (every
// caller except `lowerFragmentSubpassLoad` still does).
TEST_F(ImageSamplingTest, LoadFetchesSample0OfMultisampledTexel) {
  // A 2x1, 4-sample R32_FLOAT image: texel (0, 0)'s 4 samples are 1, 2,
  // 3, 4; texel (1, 0)'s are 5, 6, 7, 8.
  float Storage[1][2][4] = {{{1, 2, 3, 4}, {5, 6, 7, 8}}};
  FemeImageSubresourceLayout Layout{};
  Layout.RowPitch = 2 * 4 * sizeof(float);
  Layout.SlicePitch = Layout.RowPitch;
  Layout.SampleStride = sizeof(float);
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Img.Width = 2;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 4;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  Fn(ImageHeap, 1, 0, 1, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 5.0f);
}

// Roadmap F8c: an explicit, non-zero `Sample` argument reads another
// sample of the same texel -- the addressing `femeRTFetchTexel2D` gained
// in F8b (above) is exercised for real, not just at the default sample 0.
// Same image layout as `LoadFetchesSample0OfMultisampledTexel` above, but
// every one of texel (0, 0)'s 4 samples is checked individually.
TEST_F(ImageSamplingTest, LoadFetchesExplicitSampleOfMultisampledTexel) {
  float Storage[1][1][4] = {{{10, 20, 30, 40}}};
  FemeImageSubresourceLayout Layout{};
  Layout.RowPitch = 4 * sizeof(float);
  Layout.SlicePitch = Layout.RowPitch;
  Layout.SampleStride = sizeof(float);
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32_FLOAT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 4;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadFn Fn =
      resolve<LoadFn>(addWrapper("load", "feme.cpu.image.load.2d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 10.0f);
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/1, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 20.0f);
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/2, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 30.0f);
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/3, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 40.0f);
}

TEST_F(ImageSamplingTest, InactiveLaneReadsZero) {
  float Storage[1][1][4] = {{{1, 1, 1, 1}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor SamplerHeap[1] = {
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge)};

  SampleFn Fn =
      resolve<SampleFn>(addWrapper("sample", "feme.cpu.image.sample.2d.v4f32"));
  float Out[4] = {9, 9, 9, 9};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, true, /*Bias=*/0.0f,0,0,-std::numeric_limits<float>::infinity(), /*Mask=*/false, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
}

// Roadmap E26: `feme.cpu.image.load.2d.v4i32`, the integer counterpart of
// `feme.cpu.image.load.2d.v4f32` above -- each exercises one of the
// mandatory-sampled `_UINT`/`_SINT` formats `femeRTUnpackImageTexelI32`
// decodes.

// Roadmap H19g: `femeRTFetchTexel2DI32`'s own `Sample` operand -- the
// integer counterpart of `LoadFetchesExplicitSampleOfMultisampledTexel`
// above, confirming the widening actually reads each sample distinctly.
TEST_F(ImageSamplingTest, LoadI32FetchesExplicitSampleOfMultisampledTexel) {
  int32_t Storage[1][1][4] = {{{10, 20, 30, 40}}};
  FemeImageSubresourceLayout Layout{};
  Layout.RowPitch = 4 * sizeof(int32_t);
  Layout.SlicePitch = Layout.RowPitch;
  Layout.SampleStride = sizeof(int32_t);
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32_UINT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 4;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 10);
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/2, true, Out);
  EXPECT_EQ(Out[0], 30);
}

TEST_F(ImageSamplingTest, LoadI32FetchesIdentityFormat) {
  // `R32G32B32A32_UINT`/`_SINT` need no scalar conversion: the four 32-bit
  // lanes are reinterpreted directly.
  int32_t Storage[1][1][4] = {{{1, -2, 3, -4}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32G32B32A32_SINT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 1);
  EXPECT_EQ(Out[1], -2);
  EXPECT_EQ(Out[2], 3);
  EXPECT_EQ(Out[3], -4);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR8G8B8A8Sint) {
  int8_t Bytes[4] = {-1, 2, -3, 4};
  uint32_t Storage[1][1];
  memcpy(Storage, Bytes, sizeof(Bytes));
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R8G8B8A8_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], -1);
  EXPECT_EQ(Out[1], 2);
  EXPECT_EQ(Out[2], -3);
  EXPECT_EQ(Out[3], 4);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR16G16B16A16Uint) {
  uint16_t Storage[1][1][4] = {{{1, 2, 3, 65535}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R16G16B16A16_UINT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 1);
  EXPECT_EQ(Out[1], 2);
  EXPECT_EQ(Out[2], 3);
  EXPECT_EQ(Out[3], 65535); // Zero-extended, not sign-extended.
}

TEST_F(ImageSamplingTest, LoadI32FetchesR8Uint) {
  uint8_t Storage[1][1] = {{255}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_UINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 255); // Zero-extended, not sign-extended.
  EXPECT_EQ(Out[1], 0);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR8Sint) {
  int8_t Storage[1][1] = {{-1}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], -1); // Sign-extended, not zero-extended.
  EXPECT_EQ(Out[1], 0);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR8G8Uint) {
  uint8_t Storage[1][1][2] = {{{255, 128}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8G8_UINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 255); // Zero-extended, not sign-extended.
  EXPECT_EQ(Out[1], 128);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR8G8Sint) {
  int8_t Storage[1][1][2] = {{{-1, -2}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8G8_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], -1); // Sign-extended, not zero-extended.
  EXPECT_EQ(Out[1], -2);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR16Uint) {
  uint16_t Storage[1][1] = {{65535}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_UINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 65535); // Zero-extended, not sign-extended.
  EXPECT_EQ(Out[1], 0);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR16Sint) {
  uint16_t Storage[1][1] = {{(uint16_t)-1}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], -1); // Sign-extended, not zero-extended.
  EXPECT_EQ(Out[1], 0);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR16G16Uint) {
  uint16_t Storage[1][1][2] = {{{65535, 128}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_UINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 65535); // Zero-extended, not sign-extended.
  EXPECT_EQ(Out[1], 128);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR16G16Sint) {
  uint16_t Storage[1][1][2] = {{{(uint16_t)-1, (uint16_t)-2}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], -1); // Sign-extended, not zero-extended.
  EXPECT_EQ(Out[1], -2);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

// (Roadmap H19n) `R32G32_UINT`/`_SINT`: the storage-mandatory two-component
// partial siblings of `R32G32B32A32_{UINT,SINT}`. Identity format, no
// scalar conversion needed -- confirms the two 32-bit lanes round-trip
// directly, unlike every narrower-scalar format above.
TEST_F(ImageSamplingTest, LoadI32FetchesR32G32Uint) {
  uint32_t Storage[1][1][2] = {{{4000000000u, 128u}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R32G32_UINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ((uint32_t)Out[0], 4000000000u);
  EXPECT_EQ(Out[1], 128);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR32G32Sint) {
  int32_t Storage[1][1][2] = {{{-1, -2}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R32G32_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], -1);
  EXPECT_EQ(Out[1], -2);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 1);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR10G10B10A2Uint) {
  // From the MSB down: 2 bits of A, 10 bits of B, 10 bits of G, 10 bits of
  // R -- A = 3, B = 512, G = 256, R = 1.
  uint32_t Storage[1][1] = {{(3u << 30) | (512u << 20) | (256u << 10) | 1u}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R10G10B10A2_UINT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 1);
  EXPECT_EQ(Out[1], 256);
  EXPECT_EQ(Out[2], 512);
  EXPECT_EQ(Out[3], 3);
}

TEST_F(ImageSamplingTest, LoadI32FetchesR10G10B10A2Sint) {
  // Roadmap H19o: the same raw bit pattern
  // `LoadI32FetchesR10G10B10A2Uint` above reads (A = 0b11, B = 0x200,
  // G = 0x100, R = 1), but this time decoded as signed fields -- R's top
  // bit (bit 9) is clear, so it stays +1 the same as the UINT case; G's
  // top bit is also clear (0x100 = bit 8 only), so it stays +256 too; but
  // B's top bit (bit 9, since B = 0x200) is set, sign-extending to -512
  // (not +512 like the UINT case above); and A's both bits are set
  // (0b11), sign-extending the 2-bit field to -1 (not +3). This is the
  // exact asymmetry `femeRTUnpackR10G10B10A2Sint`'s own comment discusses
  // -- reusing `R10G10B10A2_UINT`'s zero-extending unpack would have
  // silently produced the wrong (positive) values for B and A here.
  uint32_t Storage[1][1] = {{(3u << 30) | (512u << 20) | (256u << 10) | 1u}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R10G10B10A2_SINT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 1);
  EXPECT_EQ(Out[1], 256);
  EXPECT_EQ(Out[2], -512);
  EXPECT_EQ(Out[3], -1);
}

TEST_F(ImageSamplingTest, LoadI32OutOfRangeCoordinateReadsZero) {
  int32_t Storage[1][1][4] = {{{1, 2, 3, 4}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32G32B32A32_UINT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4] = {5, 5, 5, 5};
  Fn(ImageHeap, 1, 0, /*X=*/1, /*Y=*/0, 0, /*Sample=*/0, true, Out);
  EXPECT_EQ(Out[0], 0);
  EXPECT_EQ(Out[1], 0);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 0);
}

TEST_F(ImageSamplingTest, LoadI32InactiveLaneReadsZero) {
  int32_t Storage[1][1][4] = {{{1, 2, 3, 4}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32G32B32A32_UINT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadI32Fn Fn = resolve<LoadI32Fn>(
      addWrapper("load_i32", "feme.cpu.image.load.2d.v4i32"));
  int32_t Out[4] = {9, 9, 9, 9};
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Sample=*/0, /*Mask=*/false, Out);
  EXPECT_EQ(Out[0], 0);
  EXPECT_EQ(Out[1], 0);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 0);
}

// Roadmap H7b-a: `Texture2DArray` sampling/fetch and `TextureCube`/
// `TextureCubeArray` sampling. Every image below is single-mip, one texel
// per face/layer (so point sampling any (U, V) inside `[0, 1]` reads that
// layer's one texel exactly), each layer's texel value equal to its own
// layer index -- isolating exactly the new `Layer`/face-selection
// addressing this milestone adds, independent of the (already-tested)
// filtering/addressing math above.

TEST_F(ImageSamplingTest, Sample2DArrayReadsRequestedLayer) {
  float Storage[3][1][1][4] = {
      {{{0, 0, 0, 0}}}, {{{1, 1, 1, 1}}}, {{{2, 2, 2, 2}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 3,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleArrayFn Fn = resolve<SampleArrayFn>(
      addWrapper("sample_array", "feme.cpu.image.sample.2darray.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/2.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/true, /*Bias=*/0.0f, /*OffsetX=*/0, /*OffsetY=*/0,
     /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, Out);
  EXPECT_FLOAT_EQ(Out[0], 2.0f);
}

TEST_F(ImageSamplingTest, Sample2DArrayRoundsLayerToNearest) {
  float Storage[2][1][1][4] = {{{{0, 0, 0, 0}}}, {{{1, 1, 1, 1}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 2,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleArrayFn Fn = resolve<SampleArrayFn>(
      addWrapper("sample_array", "feme.cpu.image.sample.2darray.v4f32"));
  float Out[4];
  // 0.6 rounds to nearest layer 1, not truncates to layer 0.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/0.6f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/true, /*Bias=*/0.0f, /*OffsetX=*/0, /*OffsetY=*/0,
     /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
}

TEST_F(ImageSamplingTest, Sample2DArrayHonorsNonZeroTexelOffset) {
  // Roadmap L33: a real, nonzero (OffsetX, OffsetY) shifts every tap's
  // own integer address by that amount before the sampler's addressing
  // mode is applied, mirroring `femeCpuImageSample2DV4F32`'s own
  // `Sample2DHonorsNonZeroTexelOffset`-shaped coverage. Point-sampling
  // texel (0, 0) of layer 1 with an offset of (1, 0) must instead read
  // texel (1, 0) of that same layer.
  float Storage[2][1][2][4] = {{{{0, 0, 0, 0}, {1, 1, 1, 1}}},
                               {{{2, 2, 2, 2}, {3, 3, 3, 3}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 1, 2,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleArrayFn Fn = resolve<SampleArrayFn>(
      addWrapper("sample_array", "feme.cpu.image.sample.2darray.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, 0.5f, /*ArrayLayer=*/1.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/true, /*Bias=*/0.0f, /*OffsetX=*/1, /*OffsetY=*/0,
     /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, Out);
  EXPECT_FLOAT_EQ(Out[0], 3.0f);
}

// Roadmap L52a: ordinary (non-comparison) `Texture1D`/`Texture1DArray`
// sampling -- isolating the new `Sample1D`/`Sample1DArray` filtering path
// (`femeRTSampleFiltered1D` and friends) independent of the already-tested
// 2D filtering/addressing math above.

TEST_F(ImageSamplingTest, Sample1DLinearBlendsTwoTexels) {
  // Sampling exactly at the shared boundary of a 2-texel 1D image must
  // average both texels equally, mirroring `LinearSampleBlendsFourTexels`
  // above but along a single axis.
  float Storage[2][4] = {{0, 0, 0, 0}, {4, 4, 4, 4}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1D(
      Storage, sizeof(Storage), 2, ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DFn Fn = resolve<Sample1DFn>(
      addWrapper("sample_1d", "feme.cpu.image.sample.1d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     0.0f, true, 0.0f, /*Offset=*/0, -std::numeric_limits<float>::infinity(),
     true, Out);
  EXPECT_FLOAT_EQ(Out[0], 2.0f);
}

TEST_F(ImageSamplingTest, Sample1DPointSampleReadsExactTexel) {
  // Point-sampling texel 1's center (normalized U = 0.75 of a 2-wide 1D
  // image) must read that texel exactly, with no blending from its
  // neighbor -- mirroring `PointSampleIdentityFormat` above.
  float Storage[2][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1D(
      Storage, sizeof(Storage), 2, ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DFn Fn = resolve<Sample1DFn>(
      addWrapper("sample_1d", "feme.cpu.image.sample.1d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.75f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     0.0f, true, 0.0f, /*Offset=*/0, -std::numeric_limits<float>::infinity(),
     true, Out);
  EXPECT_FLOAT_EQ(Out[0], 5.0f);
  EXPECT_FLOAT_EQ(Out[1], 6.0f);
  EXPECT_FLOAT_EQ(Out[2], 7.0f);
  EXPECT_FLOAT_EQ(Out[3], 8.0f);
}

TEST_F(ImageSamplingTest, Sample1DArrayReadsRequestedLayer) {
  // Mirrors `Sample2DArrayReadsRequestedLayer` above, narrowed to a
  // single spatial axis.
  float Storage[3][1][4] = {{{0, 0, 0, 0}}, {{1, 1, 1, 1}}, {{2, 2, 2, 2}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1DArray(
      Storage, sizeof(Storage), 1, 3, ResourceFormat::R32G32B32A32_FLOAT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DArrayFn Fn = resolve<Sample1DArrayFn>(
      addWrapper("sample_1d_array", "feme.cpu.image.sample.1darray.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*ArrayLayer=*/2.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, 0.0f, true, 0.0f, /*Offset=*/0,
     -std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 2.0f);
}

TEST_F(ImageSamplingTest, Sample1DInactiveLaneReadsZero) {
  // Mirrors the existing `Load*InactiveLaneReadsZero` tests' own
  // `Mask=false` convention.
  float Storage[1][4] = {{1, 2, 3, 4}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1D(
      Storage, sizeof(Storage), 1, ResourceFormat::R32G32B32A32_FLOAT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DFn Fn = resolve<Sample1DFn>(
      addWrapper("sample_1d", "feme.cpu.image.sample.1d.v4f32"));
  float Out[4] = {9, 9, 9, 9};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     0.0f, true, 0.0f, /*Offset=*/0, -std::numeric_limits<float>::infinity(),
     /*Mask=*/false, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 0.0f);
}

TEST_F(ImageSamplingTest, Sample1DBiasSelectsCoarserMipLevel) {
  // Roadmap L61(c): the `Bias` parameter `femeCpuImageSample1DV4F32`
  // gained -- mirroring `ImplicitLodBiasSelectsCoarserMipLevel`'s own
  // `Plain2D` precedent, narrowed to a single spatial axis. With zero
  // screen-space derivatives (no measurable minification of its own), a
  // `Bias` of exactly `1.0` must select mip level 1 outright
  // (`MipFilter=Nearest` rounds `0 + 1.0` up to level 1).
  float Level0[2][4] = {{1, 1, 1, 1}, {1, 1, 1, 1}};
  float Level1[1][4] = {{9, 9, 9, 9}};
  struct {
    float L0[2][4];
    float L1[1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture1D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DFn Fn = resolve<Sample1DFn>(
      addWrapper("sample_1d", "feme.cpu.image.sample.1d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Bias=*/1.0f, /*Offset=*/0,
     -std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, Sample1DRealDerivativeSelectsCoarserMipLevel) {
  // Roadmap L63: mirrors `ImplicitLodMinifyingUsesMinFilterNotMagFilter`'s
  // own `Plain2D` precedent (a real, nonzero `DUdX` alone -- no `Bias` --
  // must select a coarser mip level), narrowed to a single spatial axis:
  // a per-pixel `dU/dx` of `1.0` across this 2-texel-wide 1D image is a
  // scale factor of 2 texels/pixel (`log2(2) == 1 > 0`), so mip level 1
  // must be selected even with `Bias` and `MinLodClamp` both no-ops --
  // confirming `femeRTPlanImplicitLod1D` (roadmap L63) actually consults
  // its own `DUdX`/`DUdY` arguments, not just `Bias`/`MinLodClamp` like
  // `Sample1DBiasSelectsCoarserMipLevel` above already covers.
  float Level0[2][4] = {{1, 1, 1, 1}, {1, 1, 1, 1}};
  float Level1[1][4] = {{9, 9, 9, 9}};
  struct {
    float L0[2][4];
    float L1[1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture1D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DFn Fn = resolve<Sample1DFn>(
      addWrapper("sample_1d", "feme.cpu.image.sample.1d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/1.0f, /*DUdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Bias=*/0.0f, /*Offset=*/0,
     -std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, Sample1DArrayMinLodClampRaisesImplicitLevel) {
  // Roadmap L61(c): the `MinLodClamp` parameter
  // `femeCpuImageSample1DArrayV4F32` gained -- mirroring
  // `MinLodClampsExplicitSampleAboveBaseLevel`'s own `Plain2D` precedent,
  // narrowed to a single spatial axis. An implicit-LOD sample that would
  // otherwise resolve to level 0 (no derivatives) must instead be raised
  // to level 1 by a `MinLodClamp` of `1.0`.
  float Level0[1][2][4] = {{{1, 1, 1, 1}, {1, 1, 1, 1}}};
  float Level1[1][1][4] = {{{9, 9, 9, 9}}};
  struct {
    float L0[1][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture1D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DArrayFn Fn = resolve<Sample1DArrayFn>(
      addWrapper("sample_1d_array", "feme.cpu.image.sample.1darray.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/false,
     /*Bias=*/0.0f, /*Offset=*/0, /*MinLodClamp=*/1.0f, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, Sample1DHonorsNonZeroTexelOffset) {
  // Roadmap L66(d): a real, nonzero scalar `Offset` shifts the tap's own
  // integer address by that amount before the sampler's addressing mode
  // is applied, mirroring `Sample3DHonorsNonZeroTexelOffset`'s own
  // `Plain3D` coverage, narrowed to a single spatial axis. Point-sampling
  // texel 0 of a 2-wide 1D image with an offset of 1 must instead read
  // texel 1.
  float Storage[2][4] = {{0, 0, 0, 0}, {9, 9, 9, 9}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1D(
      Storage, sizeof(Storage), 2, ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DFn Fn = resolve<Sample1DFn>(
      addWrapper("sample_1d", "feme.cpu.image.sample.1d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f, /*Offset=*/1,
     -std::numeric_limits<float>::infinity(), /*Mask=*/true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, Sample1DArrayHonorsNonZeroTexelOffset) {
  // Roadmap L66(d): the `Array1D` counterpart immediately above --
  // confirming the real scalar `Offset` shifts only the `U` axis, never
  // `ArrayLayer` (mirroring the "SPIR-V's own `ConstOffset` dimensionality
  // excludes the array layer" rule `isSupportedOffset`'s own comment
  // documents).
  float Storage[2][2][4] = {{{0, 0, 0, 0}, {0, 0, 0, 0}},
                            {{0, 0, 0, 0}, {9, 9, 9, 9}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1DArray(Storage, sizeof(Storage), 2, 2,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample1DArrayFn Fn = resolve<Sample1DArrayFn>(
      addWrapper("sample_1d_array", "feme.cpu.image.sample.1darray.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, /*ArrayLayer=*/1.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/true,
     /*Bias=*/0.0f, /*Offset=*/1, -std::numeric_limits<float>::infinity(),
     /*Mask=*/true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

// Roadmap L66(a): ordinary (non-comparison) `Texture3D` sampling --
// isolating the new `Sample3D` filtering path (`femeRTSampleFiltered3D`
// and friends) independent of the already-tested 1D/2D filtering/
// addressing math above.

TEST_F(ImageSamplingTest, Sample3DPointSampleReadsExactTexel) {
  // Point-sampling the corner texel (1,1,1) of a 2x2x2 volume (normalized
  // U=V=W=0.75) must read that texel exactly, with no blending from any
  // neighbor -- mirroring `Sample1DPointSampleReadsExactTexel`'s own
  // precedent, extended to a third axis.
  float Storage[2][2][2][4] = {
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {9, 9, 9, 9}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage3D(Storage, sizeof(Storage), 2, 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample3DFn Fn = resolve<Sample3DFn>(
      addWrapper("sample_3d", "feme.cpu.image.sample.3d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.75f, 0.75f, 0.75f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*DWdX=*/0.0f,
     /*DWdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,
     /*OffsetX=*/0, /*OffsetY=*/0, /*OffsetZ=*/0,
     -std::numeric_limits<float>::infinity(), /*Mask=*/true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
  EXPECT_FLOAT_EQ(Out[1], 9.0f);
  EXPECT_FLOAT_EQ(Out[2], 9.0f);
  EXPECT_FLOAT_EQ(Out[3], 9.0f);
}

TEST_F(ImageSamplingTest, Sample3DLinearBlendsEightTexels) {
  // Sampling exactly at the shared corner of all 8 texels of a 2x2x2
  // volume must average all 8 equally -- the real trilinear counterpart
  // of `LinearSampleBlendsFourTexels`'s own bilinear precedent, exercising
  // `femeRTSampleLinear3D`'s full 8-corner blend (two bilinear blends at
  // Z0/Z1, then a final blend along W) rather than just its 2D halves.
  float Storage[2][2][2][4] = {
      {{{0, 0, 0, 0}, {2, 2, 2, 2}}, {{4, 4, 4, 4}, {6, 6, 6, 6}}},
      {{{8, 8, 8, 8}, {10, 10, 10, 10}}, {{12, 12, 12, 12}, {14, 14, 14, 14}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage3D(Storage, sizeof(Storage), 2, 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample3DFn Fn = resolve<Sample3DFn>(
      addWrapper("sample_3d", "feme.cpu.image.sample.3d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*DWdX=*/0.0f,
     /*DWdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,
     /*OffsetX=*/0, /*OffsetY=*/0, /*OffsetZ=*/0,
     -std::numeric_limits<float>::infinity(), /*Mask=*/true, Out);
  // Average of 0,2,4,6,8,10,12,14 is 7.
  EXPECT_FLOAT_EQ(Out[0], 7.0f);
}

TEST_F(ImageSamplingTest, Sample3DInactiveLaneReadsZero) {
  // Mirrors `Sample1DInactiveLaneReadsZero`'s own `Mask=false` convention.
  float Storage[1][1][1][4] = {{{{1, 2, 3, 4}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage3D(Storage, sizeof(Storage), 1, 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample3DFn Fn = resolve<Sample3DFn>(
      addWrapper("sample_3d", "feme.cpu.image.sample.3d.v4f32"));
  float Out[4] = {9, 9, 9, 9};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*DWdX=*/0.0f,
     /*DWdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,
     /*OffsetX=*/0, /*OffsetY=*/0, /*OffsetZ=*/0,
     -std::numeric_limits<float>::infinity(), /*Mask=*/false, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 0.0f);
}

TEST_F(ImageSamplingTest, Sample3DBiasSelectsCoarserMipLevel) {
  // Roadmap L67(a): the `Bias`/`MinLodClamp` pair `createSample3D`/
  // `femeCpuImageSample3DV4F32` just gained -- mirroring
  // `Sample1DBiasSelectsCoarserMipLevel`'s own `Plain1D` precedent,
  // extended to a third axis. With zero screen-space derivatives (no
  // measurable minification of its own), a `Bias` of exactly `1.0` must
  // select mip level 1 outright (`MipFilter=Nearest` rounds `0 + 1.0` up
  // to level 1) of a 2x2x2 -> 1x1x1 volume.
  float Level0[2][2][2][4] = {
      {{{1, 1, 1, 1}, {1, 1, 1, 1}}, {{1, 1, 1, 1}, {1, 1, 1, 1}}},
      {{{1, 1, 1, 1}, {1, 1, 1, 1}}, {{1, 1, 1, 1}, {1, 1, 1, 1}}}};
  float Level1[1][1][1][4] = {{{{9, 9, 9, 9}}}};
  struct {
    float L0[2][2][2][4];
    float L1[1][1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/2 * 2 * 4 * sizeof(float), /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/1 * 1 * 4 * sizeof(float), /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture3D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 2;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample3DFn Fn = resolve<Sample3DFn>(
      addWrapper("sample_3d", "feme.cpu.image.sample.3d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*DWdX=*/0.0f,
     /*DWdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Bias=*/1.0f,
     /*OffsetX=*/0, /*OffsetY=*/0, /*OffsetZ=*/0,
     -std::numeric_limits<float>::infinity(), /*Mask=*/true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

TEST_F(ImageSamplingTest, Sample3DHonorsNonZeroTexelOffset) {
  // Roadmap L67(c): a real, nonzero `(OffsetX, OffsetY, OffsetZ)` shifts
  // every tap's own integer address by that amount before the sampler's
  // addressing mode is applied, mirroring
  // `Sample2DArrayHonorsNonZeroTexelOffset`'s own `Array2D` coverage,
  // extended to a third, depth axis. Point-sampling texel (0, 0, 0) of a
  // 2x2x2 volume with an offset of (1, 1, 1) must instead read the
  // opposite corner texel (1, 1, 1).
  float Storage[2][2][2][4] = {
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {0, 0, 0, 0}}},
      {{{0, 0, 0, 0}, {0, 0, 0, 0}}, {{0, 0, 0, 0}, {9, 9, 9, 9}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage3D(Storage, sizeof(Storage), 2, 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  Sample3DFn Fn = resolve<Sample3DFn>(
      addWrapper("sample_3d", "feme.cpu.image.sample.3d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, 0.25f, 0.25f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f, /*DWdX=*/0.0f,
     /*DWdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/true, /*Bias=*/0.0f,
     /*OffsetX=*/1, /*OffsetY=*/1, /*OffsetZ=*/1,
     -std::numeric_limits<float>::infinity(), /*Mask=*/true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

// Roadmap L54: depth-comparison `Texture1D`/`Texture1DArray` sampling --
// isolating the new `SampleCmp1D`/`SampleCmpArray1D` comparison-filtering
// path (`femeRTSampleCmp1DAtLevel` and friends) independent of the
// already-tested 2D dref-sampling math above.

TEST_F(ImageSamplingTest, SampleCmp1DLessEqualPasses) {
  // Mirrors `ComparisonSamplingLessEqualPasses` above, narrowed to a
  // single spatial axis: a single depth texel of 0.5, compared against
  // both a passing (0.4) and a failing (0.6) reference.
  float Storage[1][4] = {{0.5f, 0, 0, 0}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1D(Storage, sizeof(Storage), 1,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmp1DFn Fn = resolve<SampleCmp1DFn>(
      addWrapper("samplecmp_1d", "feme.cpu.image.samplecmp.1d.f32"));
  float PassResult = 0.0f, FailResult = 1.0f;
  // Ref (0.4) <= Texel (0.5): pass.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     0.0f, true, 0.4f,
     /*Bias=*/0.0f, /*Offset=*/0,
     /*MinLodClamp=*/-std::numeric_limits<float>::infinity(), true,
     &PassResult);
  EXPECT_FLOAT_EQ(PassResult, 1.0f);
  // Ref (0.6) <= Texel (0.5): fail.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     0.0f, true, 0.6f,
     /*Bias=*/0.0f, /*Offset=*/0,
     /*MinLodClamp=*/-std::numeric_limits<float>::infinity(), true,
     &FailResult);
  EXPECT_FLOAT_EQ(FailResult, 0.0f);
}

TEST_F(ImageSamplingTest, SampleCmpArray1DReadsRequestedLayer) {
  // Mirrors `Sample1DArrayReadsRequestedLayer` above, but comparing each
  // layer's own depth texel against a fixed reference instead of reading
  // its raw value: `Ref (0.5) <= Texel` fails for layer 0's own depth
  // (0.2, below the reference) and passes for layer 2's (0.8, above it),
  // proving the requested array layer -- not just layer 0 -- is the one
  // actually fetched and compared.
  float Storage[3][1][4] = {
      {{0.2f, 0, 0, 0}}, {{0.5f, 0, 0, 0}}, {{0.8f, 0, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1DArray(
      Storage, sizeof(Storage), 1, 3, ResourceFormat::R32G32B32A32_FLOAT,
      Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpArray1DFn Fn = resolve<SampleCmpArray1DFn>(addWrapper(
      "samplecmp_1d_array", "feme.cpu.image.samplecmp.1darray.f32"));
  float LayerZeroResult = 1.0f, LayerTwoResult = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, 0.0f, true, 0.5f, /*Bias=*/0.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     true, &LayerZeroResult);
  EXPECT_FLOAT_EQ(LayerZeroResult, 0.0f); // Ref 0.5 <= Texel 0.2: fail.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*ArrayLayer=*/2.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, 0.0f, true, 0.5f, /*Bias=*/0.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     true, &LayerTwoResult);
  EXPECT_FLOAT_EQ(LayerTwoResult, 1.0f); // Ref 0.5 <= Texel 0.8: pass.
}

TEST_F(ImageSamplingTest, SampleCmp1DInactiveLaneReadsZero) {
  // Mirrors `Sample1DInactiveLaneReadsZero`'s own `Mask=false` convention.
  float Storage[1][4] = {{0.5f, 0, 0, 0}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1D(Storage, sizeof(Storage), 1,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmp1DFn Fn = resolve<SampleCmp1DFn>(
      addWrapper("samplecmp_1d", "feme.cpu.image.samplecmp.1d.f32"));
  float Result = 9.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     0.0f, true, 0.4f,
     /*Bias=*/0.0f, /*Offset=*/0,
     /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/false, &Result);
  EXPECT_FLOAT_EQ(Result, 0.0f);
}

TEST_F(ImageSamplingTest, SampleCmp1DHonorsNonZeroTexelOffset) {
  // Roadmap L66(k): a real, nonzero scalar `Offset` shifts the depth-
  // comparison tap's own integer address by that amount before the
  // sampler's addressing mode is applied, mirroring
  // `Sample1DHonorsNonZeroTexelOffset`'s own ordinary-sample precedent --
  // a two-texel depth image whose texel 0 (0.1) fails a `LessEqual (0.5)`
  // comparison and texel 1 (0.9) passes it, so a passing result with
  // `U=0.25` (which addresses texel 0 without the offset) can only mean
  // the `Offset` of 1 shifted the tap to texel 1.
  float Storage[2][4] = {{0.1f, 0, 0, 0}, {0.9f, 0, 0, 0}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1D(Storage, sizeof(Storage), 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmp1DFn Fn = resolve<SampleCmp1DFn>(
      addWrapper("samplecmp_1d", "feme.cpu.image.samplecmp.1d.f32"));
  float Result = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/true, /*Dref=*/0.5f, /*Bias=*/0.0f,
     /*Offset=*/1, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, &Result);
  EXPECT_FLOAT_EQ(Result, 1.0f); // Ref 0.5 <= Texel 0.9: pass.
}

TEST_F(ImageSamplingTest, SampleCmpArray1DHonorsNonZeroTexelOffset) {
  // Roadmap L66(k): the `Array1D` counterpart immediately above --
  // confirming the real scalar `Offset` shifts only the `U` axis, never
  // `ArrayLayer`, mirroring
  // `Sample1DArrayHonorsNonZeroTexelOffset`'s own identical
  // ordinary-sample precedent.
  float Storage[2][2][4] = {{{0.1f, 0, 0, 0}, {0.1f, 0, 0, 0}},
                            {{0.1f, 0, 0, 0}, {0.9f, 0, 0, 0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1DArray(Storage, sizeof(Storage), 2, 2,
                                             ResourceFormat::R32G32B32A32_FLOAT,
                                             Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpArray1DFn Fn = resolve<SampleCmpArray1DFn>(
      addWrapper("samplecmp_1d_array", "feme.cpu.image.samplecmp.1darray.f32"));
  float Result = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, /*ArrayLayer=*/1.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/true,
     /*Dref=*/0.5f, /*Bias=*/0.0f, /*Offset=*/1,
     /*MinLodClamp=*/-std::numeric_limits<float>::infinity(), /*Mask=*/true,
     &Result);
  EXPECT_FLOAT_EQ(Result, 1.0f); // Ref 0.5 <= Texel 0.9: pass.
}

// Roadmap L62: the `Bias`/`MinLodClamp` pair `femeCpuImageSampleCmp1DF32`
// and `femeCpuImageSampleCmpArray1DF32` gained. Both tests below build a
// two-level 1D depth image whose level 0 and level 1 texels sit on
// *opposite* sides of the comparison reference, so the boolean result
// alone reveals which mip level was actually sampled -- the only thing
// either new parameter can influence here.
static void makeTwoLevelDepth1D(void *Storage, size_t SizeInBytes,
                                size_t Level0SizeInBytes, uint32_t ArrayLayers,
                                FemeImageSubresourceLayout *Layouts,
                                FemeImageDescriptor &Img) {
  Layouts[0] = {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
                /*SlicePitch=*/0, /*SampleStride=*/0};
  Layouts[1] = {/*Offset=*/Level0SizeInBytes,
                /*RowPitch=*/1 * 4 * sizeof(float),
                /*SlicePitch=*/0, /*SampleStride=*/0};
  Img = {};
  Img.Data = Storage;
  Img.SizeInBytes = SizeInBytes;
  Img.Dimension =
      static_cast<uint32_t>(ArrayLayers > 1 ? ImageDimension::Texture1DArray
                                            : ImageDimension::Texture1D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = ArrayLayers;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | FEME_IMAGE_DEPTH;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
}

TEST_F(ImageSamplingTest, SampleCmp1DBiasSelectsCoarserMipLevel) {
  // Mirrors `Sample1DBiasSelectsCoarserMipLevel`'s own ordinary-sample
  // precedent. Level 0's depth texel (0.2) fails a `Ref (0.5) <= Texel`
  // comparison and level 1's (0.8) passes it, so a result of `1.0` can
  // only mean the `Bias` of exactly `1.0` shifted an implicit lod of
  // `0.0` up to level 1.
  struct {
    float L0[2][4];
    float L1[1][4];
  } Storage = {{{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}}, {{0.8f, 0, 0, 0}}};

  FemeImageSubresourceLayout Layouts[2];
  FemeImageDescriptor Img;
  makeTwoLevelDepth1D(&Storage, sizeof(Storage), sizeof(Storage.L0),
                      /*ArrayLayers=*/1, Layouts, Img);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmp1DFn Fn = resolve<SampleCmp1DFn>(
      addWrapper("samplecmp_1d", "feme.cpu.image.samplecmp.1d.f32"));
  float Biased = 0.0f, Unbiased = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/1.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, &Biased);
  EXPECT_FLOAT_EQ(Biased, 1.0f);
  // The same call with a zero bias still reads level 0, proving the value
  // above came from the bias and not from the image itself.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, &Unbiased);
  EXPECT_FLOAT_EQ(Unbiased, 0.0f);
}

TEST_F(ImageSamplingTest, SampleCmpArray1DMinLodClampRaisesLevel) {
  // The `MinLodClamp` counterpart of the test above, against `Array1D`:
  // an instruction-level `MinLod` of `1.0` must raise an implicit lod of
  // `0.0` to level 1, flipping the same comparison the other way.
  struct {
    float L0[2][4];
    float L1[1][4];
  } Storage = {{{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}}, {{0.8f, 0, 0, 0}}};

  FemeImageSubresourceLayout Layouts[2];
  FemeImageDescriptor Img;
  makeTwoLevelDepth1D(&Storage, sizeof(Storage), sizeof(Storage.L0),
                      /*ArrayLayers=*/1, Layouts, Img);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpArray1DFn Fn = resolve<SampleCmpArray1DFn>(
      addWrapper("samplecmp_1d_array", "feme.cpu.image.samplecmp.1darray.f32"));
  float Clamped = 0.0f, Unclamped = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/false,
     /*Dref=*/0.5f, /*Bias=*/0.0f, /*Offset=*/0, /*MinLodClamp=*/1.0f,
     /*Mask=*/true, &Clamped);
  EXPECT_FLOAT_EQ(Clamped, 1.0f);
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/false,
     /*Dref=*/0.5f, /*Bias=*/0.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, &Unclamped);
  EXPECT_FLOAT_EQ(Unclamped, 0.0f);
}

TEST_F(ImageSamplingTest, SampleCmp1DGradSelectsCoarserMipLevel) {
  // Roadmap L66(f): widens roadmap L66(c)'s `Plain2D`-only `Grad` support
  // to `Plain1D` too -- mirrors
  // `ComparisonSamplingGradSelectsCoarserMipLevel`'s own `Plain2D` precedent,
  // but through `femeCpuImageSampleCmp1DF32`'s new `DUdX`/`DUdY` scalar pair
  // instead of a `(DUdX, DVdX, DUdY, DVdY)` quartet. Same two-level depth image
  // as `SampleCmp1DBiasSelectsCoarserMipLevel` above: level 0 stores 0.2, level
  // 1 stores 0.8; a `LessEqual` compare against a reference of 0.5 fails at
  // level 0 and passes at level 1.
  struct {
    float L0[2][4];
    float L1[1][4];
  } Storage = {{{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}}, {{0.8f, 0, 0, 0}}};

  FemeImageSubresourceLayout Layouts[2];
  FemeImageDescriptor Img;
  makeTwoLevelDepth1D(&Storage, sizeof(Storage), sizeof(Storage.L0),
                      /*ArrayLayers=*/1, Layouts, Img);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmp1DFn Fn = resolve<SampleCmp1DFn>(
      addWrapper("samplecmp_1d", "feme.cpu.image.samplecmp.1d.f32"));

  // No derivatives: the base level's own 0.2 fails the 0.5 reference,
  // proving zero derivatives still degenerate to level 0.
  float NoGrad = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, &NoGrad);
  EXPECT_FLOAT_EQ(NoGrad, 0.0f);

  // A `DUdX` of exactly 1.0 against this 2-texel-wide image resolves to
  // an implicit LOD of exactly 1.0 (`femeRTPlanImplicitLod1D`'s own
  // `Ux = DUdX * Width = 2.0`, `Lod = log2(2.0) = 1.0`), rounding the
  // selected mip level up to level 1, whose own 0.8 passes the same
  // reference.
  float WithGrad = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*DUdX=*/1.0f, /*DUdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, &WithGrad);
  EXPECT_FLOAT_EQ(WithGrad, 1.0f);
}

TEST_F(ImageSamplingTest, SampleCmpArray1DGradSelectsCoarserMipLevel) {
  // Roadmap L66(f): the `Array1D` counterpart of the test just above --
  // only `U`, never `ArrayLayer`, is ever differentiated.
  struct {
    float L0[2][4];
    float L1[1][4];
  } Storage = {{{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}}, {{0.8f, 0, 0, 0}}};

  FemeImageSubresourceLayout Layouts[2];
  FemeImageDescriptor Img;
  makeTwoLevelDepth1D(&Storage, sizeof(Storage), sizeof(Storage.L0),
                      /*ArrayLayers=*/1, Layouts, Img);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpArray1DFn Fn = resolve<SampleCmpArray1DFn>(
      addWrapper("samplecmp_1d_array", "feme.cpu.image.samplecmp.1darray.f32"));

  float NoGrad = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/false,
     /*Dref=*/0.5f, /*Bias=*/0.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, &NoGrad);
  EXPECT_FLOAT_EQ(NoGrad, 0.0f);

  float WithGrad = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/1.0f, /*DUdY=*/0.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/false,
     /*Dref=*/0.5f, /*Bias=*/0.0f,
     /*Offset=*/0, /*MinLodClamp=*/-std::numeric_limits<float>::infinity(),
     /*Mask=*/true, &WithGrad);
  EXPECT_FLOAT_EQ(WithGrad, 1.0f);
}

TEST_F(ImageSamplingTest, SampleCmpArray2DGradSelectsCoarserMipLevel) {
  // Roadmap L66(g): the `Array2D` counterpart of
  // `ComparisonSamplingGradSelectsCoarserMipLevel` above -- only `U`/`V`,
  // never `ArrayLayer`, is ever differentiated, mirroring
  // `SampleCmpArray1DGradSelectsCoarserMipLevel`'s own identical `Array1D`
  // precedent. Same two-level depth image as that `Plain2D` test: level 0
  // stores 0.2, level 1 stores 0.8; a `LessEqual` compare against a
  // reference of 0.5 fails at level 0 and passes at level 1.
  float Level0[2][2][4] = {{{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}},
                           {{0.2f, 0, 0, 0}, {0.2f, 0, 0, 0}}};
  float Level1[1][1][4] = {{{0.8f, 0, 0, 0}}};
  struct {
    float L0[2][2][4];
    float L1[1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | FEME_IMAGE_DEPTH;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};

  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpArrayFn Fn = resolve<SampleCmpArrayFn>(
      addWrapper("samplecmp_array2d", "feme.cpu.image.samplecmp.2darray.f32"));

  // No derivatives: the base level's own 0.2 fails the 0.5 reference,
  // proving zero derivatives still degenerate to level 0.
  float NoGrad = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &NoGrad);
  EXPECT_FLOAT_EQ(NoGrad, 0.0f);

  // A `DUdX` of exactly 1.0 against this 2-texel-wide image resolves to
  // an implicit LOD of exactly 1.0 (mirroring
  // `ComparisonSamplingGradSelectsCoarserMipLevel`'s own identical math),
  // rounding the selected mip level up to level 1, whose own 0.8 passes
  // the same reference.
  float WithGrad = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/1.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f,
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &WithGrad);
  EXPECT_FLOAT_EQ(WithGrad, 1.0f);
}

TEST_F(ImageSamplingTest, SampleCmpCubeGradSelectsCoarserMipLevel) {
  // Roadmap L66(h): the `Cube` counterpart of
  // `ComparisonSamplingGradSelectsCoarserMipLevel` above -- a real
  // direction-vector derivative sextuple resolves through
  // `femeRTComputeCubeUVDerivatives` (the same face-local `(U, V)`
  // derivative conversion
  // `SampleCubeImplicitLodSelectsCoarserMipFromDerivatives` already exercises
  // for an ordinary cube sample) to a real, non-level-0 implicit LOD. Level 0
  // (2x2 per face) stores 0.2, level 1 (1x1 per face) stores 0.8; a `LessEqual`
  // compare against a reference of 0.5 fails at level 0 and passes at level 1,
  // mirroring `SampleCmpArray2DGradSelectsCoarserMipLevel`'s own identical
  // pass/fail math.
  float Level0[6][2][2][4];
  float Level1[6][1][1][4];
  for (unsigned Face = 0; Face < 6; ++Face) {
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Level0[Face][Y][X][C] = 0.2f;
    for (unsigned C = 0; C < 4; ++C)
      Level1[Face][0][0][C] = 0.8f;
  }
  struct {
    float L0[6][2][2][4];
    float L1[6][1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/2 * 2 * 4 * sizeof(float), /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/1 * 1 * 4 * sizeof(float), /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 6;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | FEME_IMAGE_DEPTH;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};

  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpCubeFn Fn = resolve<SampleCmpCubeFn>(
      addWrapper("samplecmp_cube_grad", "feme.cpu.image.samplecmp.cube.f32"));

  // No derivatives: the base level's own 0.2 fails the 0.5 reference,
  // proving zero derivatives still degenerate to level 0.
  float NoGrad = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*DDirXdX=*/0.0f, /*DDirXdY=*/0.0f, /*DDirYdX=*/0.0f,
     /*DDirYdY=*/0.0f, /*DDirZdX=*/0.0f, /*DDirZdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f,
     -std::numeric_limits<float>::infinity(), true, &NoGrad);
  EXPECT_FLOAT_EQ(NoGrad, 0.0f);

  // The same `DDirYdX=4.0` sextuple
  // `SampleCubeImplicitLodSelectsCoarserMipFromDerivatives` already
  // confirms resolves face 0's own real minification well past the
  // midpoint LOD, rounding the selected mip level up to level 1, whose
  // own 0.8 passes the same reference.
  float WithGrad = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*DDirXdX=*/0.0f, /*DDirXdY=*/0.0f, /*DDirYdX=*/4.0f,
     /*DDirYdY=*/0.0f, /*DDirZdX=*/0.0f, /*DDirZdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Dref=*/0.5f, /*Bias=*/0.0f,
     -std::numeric_limits<float>::infinity(), true, &WithGrad);
  EXPECT_FLOAT_EQ(WithGrad, 1.0f);
}

TEST_F(ImageSamplingTest, SampleCmpCubeArrayGradSelectsCoarserMipLevel) {
  // Roadmap L66(i): the `CubeArray` counterpart of
  // `SampleCmpCubeGradSelectsCoarserMipLevel` above -- the same real
  // direction-vector derivative sextuple resolves through
  // `femeRTComputeCubeClampedLod` (shared verbatim with `Cube`) to a
  // real, non-level-0 implicit LOD, exercised here against cube element
  // 1 (not element 0) to also confirm `ArrayLayer` selection and `Grad`
  // derivative handling compose correctly rather than one silently
  // overriding the other. Level 0 (2x2 per face) stores 0.2, level 1
  // (1x1 per face) stores 0.8; a `LessEqual` compare against a reference
  // of 0.5 fails at level 0 and passes at level 1.
  float Level0[12][2][2][4];
  float Level1[12][1][1][4];
  for (unsigned Layer = 0; Layer < 12; ++Layer) {
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Level0[Layer][Y][X][C] = 0.2f;
    for (unsigned C = 0; C < 4; ++C)
      Level1[Layer][0][0][C] = 0.8f;
  }
  struct {
    float L0[12][2][2][4];
    float L1[12][1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/2 * 2 * 4 * sizeof(float), /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/1 * 1 * 4 * sizeof(float), /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 12;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED | FEME_IMAGE_DEPTH;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};

  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpCubeArrayFn Fn = resolve<SampleCmpCubeArrayFn>(addWrapper(
      "samplecmp_cubearray_grad", "feme.cpu.image.samplecmp.cubearray.f32"));

  // No derivatives: the base level's own 0.2 fails the 0.5 reference,
  // proving zero derivatives still degenerate to level 0.
  float NoGrad = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*DDirXdX=*/0.0f, /*DDirXdY=*/0.0f, /*DDirYdX=*/0.0f,
     /*DDirYdY=*/0.0f, /*DDirZdX=*/0.0f, /*DDirZdY=*/0.0f,
     /*ArrayLayer=*/1.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/false,
     /*Dref=*/0.5f, /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(),
     true, &NoGrad);
  EXPECT_FLOAT_EQ(NoGrad, 0.0f);

  // The same `DDirYdX=4.0` sextuple `Cube`'s own identical test above
  // confirms resolves face 0's own real minification well past the
  // midpoint LOD, rounding the selected mip level up to level 1, whose
  // own 0.8 passes the same reference -- still against cube element 1.
  float WithGrad = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*DDirXdX=*/0.0f, /*DDirXdY=*/0.0f, /*DDirYdX=*/4.0f,
     /*DDirYdY=*/0.0f, /*DDirZdX=*/0.0f, /*DDirZdY=*/0.0f,
     /*ArrayLayer=*/1.0f, /*Lod=*/0.0f, /*UseExplicitLod=*/false,
     /*Dref=*/0.5f, /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(),
     true, &WithGrad);
  EXPECT_FLOAT_EQ(WithGrad, 1.0f);
}

// unclamped-lod computation, independent of the already-tested ordinary
// sampling math above. Every test below uses a single-layer, `Width ==
// Height == 4` image so a `dU/dx == 1/Width` derivative of exactly `0.25`
// maps to a clean, easily-checked texel-space footprint of `1.0`
// (`log2(1.0) == 0.0`).
TEST_F(ImageSamplingTest, QueryLod2DZeroDerivativesReportUnclampedNegativeInfinity) {
  // Roadmap L52e design note (`QLODTM_ZERO_UV_WIDTH`, VK-GL-CTS's own
  // `vktShaderRenderTextureFunctionTests.cpp`): a coordinate with no
  // measurable derivatives at all reports an unclamped raw lod of
  // `-infinity`, not `0.0` -- diverging from `femeRTPlanImplicitLod`'s
  // own ordinary-sampling convention (see `femeRTComputeUnclampedQueryLod`'s
  // doc). The clamped level still resolves to `0.0` (this sampler's own
  // default `MinLod=0.0` floor wins over the `-infinity` bias-shifted
  // value), matching that same CTS test mode's own expectation.
  float Storage[4][4][4] = {}; // Uninitialized texel contents don't matter.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 4, 4,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  QueryLod2DFn Fn = resolve<QueryLod2DFn>(
      addWrapper("querylod_2d", "feme.cpu.image.querylod.2d.v2f32"));
  float Out[2] = {1.0f, 1.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DUdX=*/0.0f, /*DUdY=*/0.0f,
     /*DVdX=*/0.0f, /*DVdY=*/0.0f, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_EQ(Out[1], -std::numeric_limits<float>::infinity());
}

TEST_F(ImageSamplingTest, QueryLod2DReportsRawLodFromDerivatives) {
  // A real, nonzero `dU/dx = 0.25` (this image's own `1/Width`) against a
  // single-mip image is the standard one-screen-pixel-per-texel texel-
  // space footprint (`Pmax == 1.0`); the clamped level is this
  // non-mipmapped image's own `MipLevels <= 1` special case, always
  // `0.0` (per `femeRTComputeClampedQueryLevel`'s doc), while the
  // unclamped lod is `femeRTFastLog2(1.0)`, exactly `0.0` (this
  // approximation is exact at every power of two) -- compared against
  // `expectedFastLog2`'s identical formula rather than asserted as a
  // literal `0.0f`, so this test still exercises the real function this
  // approximation actually is, not an assumption about it.
  float Storage[4][4][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 4, 4,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  QueryLod2DFn Fn = resolve<QueryLod2DFn>(
      addWrapper("querylod_2d", "feme.cpu.image.querylod.2d.v2f32"));
  float Out[2] = {-9.0f, -9.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DUdX=*/0.25f, /*DUdY=*/0.0f,
     /*DVdX=*/0.0f, /*DVdY=*/0.0f, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_NEAR(Out[1], expectedFastLog2(1.0f), 1e-5f);
}

TEST_F(ImageSamplingTest, QueryLod2DClampedLevelRoundsForNearestMipFilter) {
  // Roadmap L52e (`computeLevelFromLod`, VK-GL-CTS's own reference
  // oracle): a real two-mip-level image with a `mipmapMode=NEAREST`
  // (`MipFilter=Nearest`) sampler must round its own clamped level to
  // the nearest whole level -- unlike `MipFilter=Linear`, which reports
  // the continuous fractional value unrounded (see the test below).
  // `DUdX` below is chosen so the texel-space footprint is `1.6`, whose
  // `expectedFastLog2` value (~0.678, comfortably past the `NEAREST`
  // rounding threshold's own midpoint of `0.5`, and still comfortably
  // inside this two-level image's own valid `[0, 1]` range) must round
  // up to level 1.
  float Level0[4][4][4] = {};
  float Level1[2][2][4] = {};
  struct {
    float L0[4][4][4];
    float L1[2][2][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));
  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/4 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 4;
  Img.Height = 4;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  Samp.MipFilter = static_cast<uint32_t>(SamplerFilter::Nearest);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  QueryLod2DFn Fn = resolve<QueryLod2DFn>(
      addWrapper("querylod_2d", "feme.cpu.image.querylod.2d.v2f32"));
  float Out[2] = {-9.0f, -9.0f};
  // `dU/dx = 1.6 / Width`: a texel-space footprint (`Ux`) of `1.6`.
  float DUdX = 1.6f / 4.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, DUdX, 0.0f, 0.0f, 0.0f, true, Out);
  float ExpectedLod = expectedFastLog2(1.6f);
  EXPECT_NEAR(Out[1], ExpectedLod, 1e-5f); // Unclamped lod: unrounded, as-is.
  EXPECT_FLOAT_EQ(Out[0], 1.0f); // Clamped level: rounds ~0.678 up to 1.
}

TEST_F(ImageSamplingTest, QueryLod2DClampedLevelStaysFractionalForLinearMipFilter) {
  // Same image/derivative as
  // `QueryLod2DClampedLevelRoundsForNearestMipFilter` above, but this
  // sampler's own `MipFilter=Linear` (explicitly overridden below --
  // `makeSampler` itself always defaults to `Nearest`) must instead
  // report the clamped level as the same unrounded ~0.678 fractional
  // value a real trilinear sample's own two-level blend would use, not
  // round it to a whole level the way `MipFilter=Nearest` does.
  float Level0[4][4][4] = {};
  float Level1[2][2][4] = {};
  struct {
    float L0[4][4][4];
    float L1[2][2][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));
  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/4 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/0, /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 4;
  Img.Height = 4;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  // `makeSampler` always defaults `MipFilter` to `Nearest` -- override it
  // explicitly here (unlike the sibling test above, whose own explicit
  // `Nearest` override just restates that same default).
  Samp.MipFilter = static_cast<uint32_t>(SamplerFilter::Linear);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  QueryLod2DFn Fn = resolve<QueryLod2DFn>(
      addWrapper("querylod_2d", "feme.cpu.image.querylod.2d.v2f32"));
  float Out[2] = {-9.0f, -9.0f};
  float DUdX = 1.6f / 4.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, DUdX, 0.0f, 0.0f, 0.0f, true, Out);
  float ExpectedLod = expectedFastLog2(1.6f);
  EXPECT_NEAR(Out[1], ExpectedLod, 1e-5f); // Unclamped lod.
  EXPECT_NEAR(Out[0], ExpectedLod, 1e-5f); // Clamped level: unrounded, matches lod.
}

TEST_F(ImageSamplingTest, QueryLod2DInactiveLaneReadsZero) {
  // Mirrors every other entry point's own `Mask=false` convention.
  float Storage[4][4][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 4, 4,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  QueryLod2DFn Fn = resolve<QueryLod2DFn>(
      addWrapper("querylod_2d", "feme.cpu.image.querylod.2d.v2f32"));
  float Out[2] = {9.0f, 9.0f};
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, 0.0f, 0.0f, 0.0f,
     /*Mask=*/false, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
}

TEST_F(ImageSamplingTest, Load2DArrayReadsRequestedLayer) {
  float Storage[2][1][1][4] = {{{{0, 0, 0, 0}}}, {{{7, 7, 7, 7}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 2,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadArrayFn Fn = resolve<LoadArrayFn>(
      addWrapper("load_array", "feme.cpu.image.load.2darray.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Layer=*/1, /*Mip=*/0, /*Sample=*/0,
     true, Out);
  EXPECT_FLOAT_EQ(Out[0], 7.0f);
}

TEST_F(ImageSamplingTest, Load2DArrayOutOfRangeLayerReadsZero) {
  float Storage[1][1][1][4] = {{{{9, 9, 9, 9}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 1,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadArrayFn Fn = resolve<LoadArrayFn>(
      addWrapper("load_array", "feme.cpu.image.load.2darray.v4f32"));
  float Out[4] = {1, 1, 1, 1};
  Fn(ImageHeap, 1, 0, 0, 0, /*Layer=*/1, 0, 0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 0.0f);
}

TEST_F(ImageSamplingTest, Load2DArrayI32ReadsRequestedLayer) {
  int32_t Storage[2][1][1][4] = {{{{0, 0, 0, 0}}}, {{{42, 42, 42, 42}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 2,
                       ResourceFormat::R32G32B32A32_UINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadArrayI32Fn Fn = resolve<LoadArrayI32Fn>(
      addWrapper("load_array_i32", "feme.cpu.image.load.2darray.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Layer=*/1, /*Mip=*/0, /*Sample=*/0,
     true, Out);
  EXPECT_EQ(Out[0], 42);
}

// Roadmap H19m: `feme.cpu.image.load.2darray.v4i32` was widened to add a
// real `Sample` operand (mirroring `feme.cpu.image.load.2darray.v4f32`,
// which already had one) -- confirms the arrayed-*and*-multisampled read
// path threads a non-zero sample through correctly, using the same
// per-layer, per-sample manual descriptor construction the write-side
// `StoreArrayMS*` tests above use (`makeImage2DArray` does not support
// `SampleCount > 1`).
TEST_F(ImageSamplingTest, Load2DArrayI32ReadsRequestedLayerAndSample) {
  int32_t Storage[2][2][4] = {}; // [Layer][Sample][Channel].
  Storage[1][1][0] = Storage[1][1][1] = Storage[1][1][2] =
      Storage[1][1][3] = 42;
  FemeImageSubresourceLayout Layout{};
  Layout.SampleStride = 4 * sizeof(int32_t);
  Layout.RowPitch = 2 * Layout.SampleStride;
  Layout.SlicePitch = Layout.RowPitch;
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2DArray);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_UINT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 2;
  Img.PlaneCount = 1;
  Img.SampleCount = 2;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};
  LoadArrayI32Fn Fn = resolve<LoadArrayI32Fn>(
      addWrapper("load_array_ms_i32", "feme.cpu.image.load.2darray.v4i32"));
  int32_t Out[4] = {0, 0, 0, 0};
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Layer=*/1, /*Mip=*/0, /*Sample=*/1,
     true, Out);
  EXPECT_EQ(Out[0], 42);
  int32_t OtherSample[4] = {9, 9, 9, 9};
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Layer=*/1, /*Mip=*/0, /*Sample=*/0,
     true, OtherSample);
  EXPECT_EQ(OtherSample[0], 0);
}

TEST_F(ImageSamplingTest, SampleCubeSelectsEachFaceByDirection) {
  // Six single-texel faces, layer N valued N, in Vulkan's own cube
  // array-layer order (+X, -X, +Y, -Y, +Z, -Z).
  float Storage[6][1][1][4] = {{{{0, 0, 0, 0}}}, {{{1, 1, 1, 1}}},
                               {{{2, 2, 2, 2}}}, {{{3, 3, 3, 3}}},
                               {{{4, 4, 4, 4}}}, {{{5, 5, 5, 5}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 6,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::Repeat);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCubeFn Fn = resolve<SampleCubeFn>(
      addWrapper("sample_cube", "feme.cpu.image.sample.cube.v4f32"));
  struct { float X, Y, Z; float Expected; } Cases[] = {
      {1.0f, 0.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 1.0f, 0.0f, 2.0f},  {0.0f, -1.0f, 0.0f, 3.0f},
      {0.0f, 0.0f, 1.0f, 4.0f},  {0.0f, 0.0f, -1.0f, 5.0f},
  };
  for (auto &C : Cases) {
    float Out[4];
    Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, C.X, C.Y, C.Z, 0.0f, 0.0f, 0.0f,
       0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(),
       true, Out);
    EXPECT_FLOAT_EQ(Out[0], C.Expected)
        << "direction (" << C.X << ", " << C.Y << ", " << C.Z << ")";
  }
}

TEST_F(ImageSamplingTest, SampleCubeArraySelectsRequestedCubeElement) {
  // Two cube elements (12 layers total): element 0's six faces valued 0,
  // element 1's six faces valued 100.
  float Storage[12][1][1][4];
  for (unsigned I = 0; I < 6; ++I)
    for (unsigned C = 0; C < 4; ++C) {
      Storage[I][0][0][C] = 0.0f;
      Storage[I + 6][0][0][C] = 100.0f;
    }
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 12,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::Repeat);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCubeArrayFn Fn = resolve<SampleCubeArrayFn>(
      addWrapper("sample_cubearray", "feme.cpu.image.sample.cubearray.v4f32"));
  float Out0[4], Out1[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f, /*ArrayLayer=*/0.0f, 0.0f, true, /*Bias=*/0.0f,
     -std::numeric_limits<float>::infinity(), true, Out0);
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f, /*ArrayLayer=*/1.0f, 0.0f, true, /*Bias=*/0.0f,
     -std::numeric_limits<float>::infinity(), true, Out1);
  EXPECT_FLOAT_EQ(Out0[0], 0.0f);
  EXPECT_FLOAT_EQ(Out1[0], 100.0f);
}

// Roadmap L48: `feme.cpu.image.samplecmp.2darray.f32`/`.cube.f32`/
// `.cubearray.f32` -- the `Array2D`/`Cube`/`CubeArray` counterparts of
// `feme.cpu.image.samplecmp.2d.f32` (tested above by
// `ComparisonSamplingLessEqualPasses`), isolating exactly the new
// `Layer`/face-selection addressing this milestone adds to the
// depth-comparison path, mirroring `Sample2DArrayReadsRequestedLayer`/
// `SampleCubeSelectsEachFaceByDirection`/
// `SampleCubeArraySelectsRequestedCubeElement`'s own per-layer/per-face
// texel values above (each layer/face's own depth texel equal to its own
// index, scaled to `[0, 1)` so `GreaterEqual` cleanly separates them).

TEST_F(ImageSamplingTest, SampleCmpArray2DComparesRequestedLayer) {
  float Storage[3][1][1][4] = {
      {{{0.0f, 0, 0, 0}}}, {{{0.5f, 0, 0, 0}}}, {{{0.9f, 0, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 3,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout,
                       FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::GreaterEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpArrayFn Fn = resolve<SampleCmpArrayFn>(addWrapper(
      "samplecmp_array2d", "feme.cpu.image.samplecmp.2darray.f32"));
  float Result = 0.0f;
  // Ref (0.5) >= layer 1's own texel (0.5): pass.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/1.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f,
     /*Lod=*/0.0f, true, /*Dref=*/0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &Result);
  EXPECT_FLOAT_EQ(Result, 1.0f);
  // Ref (0.5) >= layer 2's own texel (0.9): fail.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/2.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f,
     /*Lod=*/0.0f, true, /*Dref=*/0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &Result);
  EXPECT_FLOAT_EQ(Result, 0.0f);
}

TEST_F(ImageSamplingTest,
      SampleCmpArray2DNonzeroOffsetShiftsFetchedTexel) {
  // Roadmap L50d: same real, nonzero `ConstOffset` proof as
  // `ComparisonSamplingNonzeroOffsetShiftsFetchedTexel` above, but against
  // `Array2D`'s own `feme.cpu.image.samplecmp.2darray.f32` entry point --
  // confirms the offset is applied to the (U, V) address *within* the
  // requested array layer, not just for the un-arrayed `Plain2D` shape.
  float Storage[1][1][2][4] = {{{{0.4f, 0, 0, 0}, {0.6f, 0, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), /*Width=*/2, /*Height=*/1,
                       /*ArrayLayers=*/1, ResourceFormat::R32G32B32A32_FLOAT,
                       Layout, FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::LessEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpArrayFn Fn = resolve<SampleCmpArrayFn>(addWrapper(
      "samplecmp_array2d", "feme.cpu.image.samplecmp.2darray.f32"));
  // No offset: (0.25, 0.5) reads texel 0 (0.4). Ref (0.5) <= 0.4: fail.
  float NoOffsetResult = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f,
     /*Lod=*/0.0f, true, /*Dref=*/0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &NoOffsetResult);
  EXPECT_FLOAT_EQ(NoOffsetResult, 0.0f);
  // A `(+1, 0)` offset shifts the same coordinate's own fetched texel to
  // texel 1 (0.6). Ref (0.5) <= 0.6: pass.
  float OffsetResult = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.25f, 0.5f, /*ArrayLayer=*/0.0f,
     /*DUdX=*/0.0f, /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.0f,
     /*Lod=*/0.0f, true, /*Dref=*/0.5f, /*Bias=*/0.0f, 1, 0,
     -std::numeric_limits<float>::infinity(), true, &OffsetResult);
  EXPECT_FLOAT_EQ(OffsetResult, 1.0f);
}

TEST_F(ImageSamplingTest, SampleCmpCubeSelectsEachFaceByDirection) {
  float Storage[6][1][1][4] = {{{{0.0f, 0, 0, 0}}}, {{{0.2f, 0, 0, 0}}},
                               {{{0.4f, 0, 0, 0}}}, {{{0.6f, 0, 0, 0}}},
                               {{{0.8f, 0, 0, 0}}}, {{{0.9f, 0, 0, 0}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 6,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout,
                       FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::Repeat);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::GreaterEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpCubeFn Fn = resolve<SampleCmpCubeFn>(
      addWrapper("samplecmp_cube", "feme.cpu.image.samplecmp.cube.f32"));
  // Face 0 (+X) reads 0.0: a 0.5 reference (Dref >= Texel) passes
  // GreaterEqual.
  float Pass = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f, 0.0f, true,
     /*Dref=*/0.5f, /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(),
     true, &Pass);
  EXPECT_FLOAT_EQ(Pass, 1.0f);
  // Face 5 (-Z) reads 0.9: a 0.5 reference fails GreaterEqual.
  float Fail = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f, 0.0f, true,
     /*Dref=*/0.5f, /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(),
     true, &Fail);
  EXPECT_FLOAT_EQ(Fail, 0.0f);
}

TEST_F(ImageSamplingTest, SampleCmpCubeArraySelectsRequestedCubeElement) {
  float Storage[12][1][1][4];
  for (unsigned I = 0; I < 6; ++I)
    for (unsigned C = 0; C < 4; ++C) {
      Storage[I][0][0][C] = 0.1f;
      Storage[I + 6][0][0][C] = 0.9f;
    }
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 1, 1, 12,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout,
                       FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::Repeat);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::GreaterEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpCubeArrayFn Fn = resolve<SampleCmpCubeArrayFn>(addWrapper(
      "samplecmp_cubearray", "feme.cpu.image.samplecmp.cubearray.f32"));
  float Element0 = 0.0f, Element1 = 1.0f;
  // Element 0's own texel (0.1) passes a 0.5 reference (Dref >= Texel).
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f, /*ArrayLayer=*/0.0f, 0.0f, true, /*Dref=*/0.5f,
     /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(), true, &Element0);
  EXPECT_FLOAT_EQ(Element0, 1.0f);
  // Element 1's own texel (0.9) fails the same reference.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f, /*ArrayLayer=*/1.0f, 0.0f, true, /*Dref=*/0.5f,
     /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(), true, &Element1);
  EXPECT_FLOAT_EQ(Element1, 0.0f);
}

// Roadmap L55: Vulkan spec 16.5 "Depth Compare Operation" requires that,
// for a *fixed-point* (normalized) depth format such as `D16_UNORM`, both
// the compare reference (`Dref`) and the fetched depth texel are clamped
// to `[0, 1]` before the comparison is applied -- matching VK-GL-CTS's
// own `execCompare` (`tcuTexture.cpp`), which gates this clamping on
// `isFixedPointDepth`. Before this fix, `femeRTApplyCompare` never
// clamped at all, so an out-of-`[0,1]` `Dref` (as real
// `dEQP-VK.glsl.texture_functions.texture.samplercubearrayshadow_fragment`
// coordinates can produce, since that CTS case's own `v_texCoord.w`
// doubles as both the cube-array layer index and the depth-compare
// `Dref`) could flip a `Less`-family compare's pass/fail outcome versus
// real hardware whenever the fetched texel happened to sit exactly at
// the `0.0`/`1.0` clamp boundary. A `D16_UNORM` texel of exactly `0.0`
// compared with `Less` against a negative `Dref` demonstrates the flip
// directly: unclamped, `-0.5 < 0.0` is trivially true (an incorrect
// pass); clamped, `Ref=clamp(-0.5,0,1)=0.0` and `Texel=clamp(0.0,0,1)=
// 0.0`, so `0.0 < 0.0` is false (the correct fail).
TEST_F(ImageSamplingTest, SampleCmp2DClampsFixedPointDepthReference) {
  uint16_t Storage[1][1] = {{0}}; // 0 / 65535 == 0.0.
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::D16_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::Less);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpFn Fn = resolve<SampleCmpFn>(
      addWrapper("samplecmp", "feme.cpu.image.samplecmp.2d.f32"));
  float Result = 1.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, true, /*Dref=*/-0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &Result);
  EXPECT_FLOAT_EQ(Result, 0.0f);
}

// The `D32_FLOAT` counterpart: a floating-point depth format must NOT be
// clamped (VK-GL-CTS's `isFixedPointDepth` is false for it), so the same
// negative `Dref` against the same `0.0` texel keeps its unclamped,
// mathematically literal `Less` result -- a real pass, unlike the
// `D16_UNORM` case immediately above. This guards against a regression
// that clamps every depth format indiscriminately.
TEST_F(ImageSamplingTest, SampleCmp2DDoesNotClampFloatDepthReference) {
  float Storage[1][1] = {{0.0f}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::D32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::Less);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpFn Fn = resolve<SampleCmpFn>(
      addWrapper("samplecmp", "feme.cpu.image.samplecmp.2d.f32"));
  float Result = 0.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, true, /*Dref=*/-0.5f, /*Bias=*/0.0f, 0, 0,
     -std::numeric_limits<float>::infinity(), true, &Result);
  EXPECT_FLOAT_EQ(Result, 1.0f);
}

// The `TextureCubeArray` counterpart, exercising `femeRTSampleCmpCubeAtLevel`
// directly -- the function actually responsible for roadmap L55's real
// `samplercubearrayshadow_fragment` mismatch (a `D16_UNORM` depth image,
// the format that CTS case's own depth attachment uses). Mirrors the 2D
// case above: a `0.0` texel on cube-array element 0 compared with `Less`
// against a negative `Dref` must fail once clamped, not incorrectly pass.
TEST_F(ImageSamplingTest, SampleCmpCubeArrayClampsFixedPointDepthReference) {
  uint16_t Storage[6][1][1] = {{{0}}, {{0}}, {{0}}, {{0}}, {{0}}, {{0}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2DArray(
      Storage, sizeof(Storage), 1, 1, 6, ResourceFormat::D16_UNORM, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::Repeat);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::Less);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpCubeArrayFn Fn = resolve<SampleCmpCubeArrayFn>(addWrapper(
      "samplecmp_cubearray", "feme.cpu.image.samplecmp.cubearray.f32"));
  float Result = 1.0f;
  // Face 0 (+X) of element 0, texel 0.0, compared Less against -0.5.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f, /*ArrayLayer=*/0.0f, 0.0f, true, /*Dref=*/-0.5f,
     /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(), true, &Result);
  EXPECT_FLOAT_EQ(Result, 0.0f);
}

// Roadmap L53: Vulkan's own spec-mandated default "seamless cube map
// filtering" -- a `LINEAR` bilinear tap that falls just outside a face's
// own bounds blends with its true geometric neighbor across the shared
// cube edge, rather than clamping back onto the same face's own edge
// texel the way `SampleCubeSelectsEachFaceByDirection`'s own `Nearest`
// filter above implicitly relies on. A 2x2-texel-per-face cube, face 0
// (+X) uniformly 100.0, face 4 (+Z) uniformly 0.0 (the two faces
// `femeRTRemapCubeEdgeCoords` connects across face 0's own low-`U`
// edge), sampled at a direction vector deliberately chosen so one of the
// four bilinear taps lands one texel past that edge (`x0 == -1`,
// `femeRTComputeCubeBilinearSupport`'s own `X0`), remapping onto face 4.
// Pre-L53, `femeRTSampleFiltered2D`'s own `ClampToEdge` addressing would
// have clamped that tap back to `x == 0` on face 0 itself, reading
// exactly `100.0` -- proving the fix requires this test to observe a
// value measurably below `100.0`.
TEST_F(ImageSamplingTest, SampleCubeSeamlessBlendsAcrossFaceEdge) {
  float Storage[6][2][2][4];
  for (unsigned Face = 0; Face < 6; ++Face)
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Storage[Face][Y][X][C] = (Face == 0) ? 100.0f : 0.0f;
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCubeFn Fn = resolve<SampleCubeFn>(
      addWrapper("sample_cube_seamless", "feme.cpu.image.sample.cube.v4f32"));
  // (X=1, Y=0, Z=0.6): selects face 0 (+X, |X| is the largest-magnitude
  // component); face 0's own U = -Z/Major places the bilinear footprint's
  // low tap one texel below `u == 0`, remapping to face 4 (+Z).
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,
     -std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_LT(Out[0], 99.0f) << "expected a blended value pulling below face "
                             "0's own uniform 100.0, not a clamped 100.0";
  EXPECT_GT(Out[0], 1.0f) << "expected face 0's own contribution to still "
                            "dominate the blend (face 0's tap has the "
                            "larger bilinear weight here)";
  EXPECT_NEAR(Out[0], 90.0f, 5.0f);
}

// The `NEAREST`-filter counterpart of `SampleCubeSeamlessBlendsAcrossFaceEdge`
// above, confirming `femeRTSampleFilteredCube`'s own documented
// short-circuit: a `NEAREST` tap never needs seamless cross-face
// blending (Vulkan's own `NEAREST` filter has no fractional footprint
// that could straddle a face edge), so the exact same direction vector
// still reads face 0's own uniform `100.0` unmodified.
TEST_F(ImageSamplingTest, SampleCubeNearestDoesNotBlendAcrossFaceEdge) {
  float Storage[6][2][2][4];
  for (unsigned Face = 0; Face < 6; ++Face)
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Storage[Face][Y][X][C] = (Face == 0) ? 100.0f : 0.0f;
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCubeFn Fn = resolve<SampleCubeFn>(
      addWrapper("sample_cube_nearest", "feme.cpu.image.sample.cube.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,
     -std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 100.0f);
}

// Roadmap L56: an implicit-LOD `TextureCube::Sample` with no measurable
// minification (every `DDir*` derivative zero -- the same "outside the
// fragment stage" scenario `ImplicitLodWithNoDerivativesReadsBaseLevel`
// exercises for `Plain2D`) must still read the base level -- this is the
// pre-existing, unregressed behavior for a caller with no real derivative
// to give.
TEST_F(ImageSamplingTest, SampleCubeImplicitLodWithNoDerivativesReadsBaseLevel) {
  // A two-level, 6-face cube mip chain: level 0 is 2x2 per face (all
  // 1s), level 1 is 1x1 per face (value 9), mirroring
  // `ImplicitLodWithNoDerivativesReadsBaseLevel`'s own two-level Plain2D
  // layout, but replicated across all 6 faces.
  float Level0[6][2][2][4];
  float Level1[6][1][1][4];
  for (unsigned Face = 0; Face < 6; ++Face) {
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Level0[Face][Y][X][C] = 1.0f;
    for (unsigned C = 0; C < 4; ++C)
      Level1[Face][0][0][C] = 9.0f;
  }
  struct {
    float L0[6][2][2][4];
    float L1[6][1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/2 * 2 * 4 * sizeof(float), /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/1 * 1 * 4 * sizeof(float), /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 6;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MipFilter = static_cast<uint32_t>(SamplerFilter::Nearest);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCubeFn Fn = resolve<SampleCubeFn>(addWrapper(
      "sample_cube_no_derivs", "feme.cpu.image.sample.cube.v4f32"));
  float Out[4];
  // Direction (1, 0, 0) selects face 0 dead-center; every `DDir*`
  // derivative operand is zero.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*DDirXdX=*/0.0f, /*DDirXdY=*/0.0f, /*DDirYdX=*/0.0f,
     /*DDirYdY=*/0.0f, /*DDirZdX=*/0.0f, /*DDirZdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(), true,
     Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
}

TEST_F(ImageSamplingTest, SampleCubeImplicitLodBiasSelectsCoarserMipLevel) {
  // Roadmap L58: the same `Bias` operand also lowers against `Cube`,
  // mirroring `ImplicitLodBiasSelectsCoarserMipLevel`'s own `Plain2D`
  // proof -- with zero screen-space derivatives, a `Bias` of exactly
  // `1.0` must select mip level 1 outright.
  float Level0[6][2][2][4];
  float Level1[6][1][1][4];
  for (unsigned Face = 0; Face < 6; ++Face) {
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Level0[Face][Y][X][C] = 1.0f;
    for (unsigned C = 0; C < 4; ++C)
      Level1[Face][0][0][C] = 9.0f;
  }
  struct {
    float L0[6][2][2][4];
    float L1[6][1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/2 * 2 * 4 * sizeof(float), /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/1 * 1 * 4 * sizeof(float), /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 6;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MipFilter = static_cast<uint32_t>(SamplerFilter::Nearest);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCubeFn Fn = resolve<SampleCubeFn>(
      addWrapper("sample_cube_bias", "feme.cpu.image.sample.cube.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*DDirXdX=*/0.0f, /*DDirXdY=*/0.0f, /*DDirYdX=*/0.0f,
     /*DDirYdY=*/0.0f, /*DDirZdX=*/0.0f, /*DDirZdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/1.0f,
     -std::numeric_limits<float>::infinity(), true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

// Roadmap L56: the core proof of this row's own fix -- an implicit-LOD
// `TextureCube::Sample` given a nonzero screen-space derivative of its
// own direction vector must now resolve to a real, non-always-zero
// implicit LOD, reading the coarser mip level exactly the way
// `ImplicitLodSelectsCoarserMipFromDerivatives` already proves for
// `Plain2D`. Before this row, `SampleCube`'s implicit-LOD path always
// hardcoded `Lod = 0` (see `ImageCalls.h`'s own former scope-note
// comment), so this same call would previously have read level 0's
// value (1.0) unconditionally, regardless of any derivative passed in.
TEST_F(ImageSamplingTest, SampleCubeImplicitLodSelectsCoarserMipFromDerivatives) {
  float Level0[6][2][2][4];
  float Level1[6][1][1][4];
  for (unsigned Face = 0; Face < 6; ++Face) {
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Level0[Face][Y][X][C] = 1.0f;
    for (unsigned C = 0; C < 4; ++C)
      Level1[Face][0][0][C] = 9.0f;
  }
  struct {
    float L0[6][2][2][4];
    float L1[6][1][1][4];
  } Storage;
  memcpy(Storage.L0, Level0, sizeof(Level0));
  memcpy(Storage.L1, Level1, sizeof(Level1));

  FemeImageSubresourceLayout Layouts[2] = {
      {/*Offset=*/0, /*RowPitch=*/2 * 4 * sizeof(float),
       /*SlicePitch=*/2 * 2 * 4 * sizeof(float), /*SampleStride=*/0},
      {/*Offset=*/sizeof(Level0), /*RowPitch=*/1 * 4 * sizeof(float),
       /*SlicePitch=*/1 * 1 * 4 * sizeof(float), /*SampleStride=*/0}};

  FemeImageDescriptor Img{};
  Img.Data = &Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 2;
  Img.Height = 2;
  Img.Depth = 1;
  Img.MipLevels = 2;
  Img.ArrayLayers = 6;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_SAMPLED;
  Img.MipLayouts = Layouts;
  Img.MipLayoutCount = 2;
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Nearest, SamplerAddressMode::ClampToEdge);
  Samp.MipFilter = static_cast<uint32_t>(SamplerFilter::Nearest);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCubeFn Fn = resolve<SampleCubeFn>(addWrapper(
      "sample_cube_derivs", "feme.cpu.image.sample.cube.v4f32"));
  // Direction (1, 0, 0) selects face 0 dead-center (U = V = 0.5 in
  // face-local terms); a large `DDirYdX` (this direction's own Y
  // component varying steeply across the screen-space X axis) is a
  // real, sharp minification once converted through face 0's own
  // quotient-rule UV derivative (face 0: U = -Z/X, V = -Y/X -- see
  // `femeRTComputeCubeUVDerivatives`), large enough to resolve well past
  // the midpoint LOD `0.5` and read the coarser (1x1) level's own value.
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.0f, /*DDirXdX=*/0.0f, /*DDirXdY=*/0.0f, /*DDirYdX=*/4.0f,
     /*DDirYdY=*/0.0f, /*DDirZdX=*/0.0f, /*DDirZdY=*/0.0f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(), true,
     Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
}

// The depth-comparison counterpart of `SampleCubeSeamlessBlendsAcrossFaceEdge`
// above: face 0 (+X) uniformly fails a `GreaterEqual(0.5)` compare (depth
// `0.0 < 0.5`), face 4 (+Z) uniformly passes it (depth `1.0 >= 0.5`); the
// same edge-straddling direction vector should read a comparison result
// strictly between `0.0` and `1.0` -- the percentage-closer-filtered
// blend of face 0's own failing taps and face 4's own passing one --
// rather than a clamped, unblended `0.0` (face 0's own uniform result).
TEST_F(ImageSamplingTest, SampleCmpCubeSeamlessBlendsAcrossFaceEdge) {
  float Storage[6][2][2][4];
  for (unsigned Face = 0; Face < 6; ++Face)
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Storage[Face][Y][X][C] = (Face == 0) ? 0.0f : 1.0f;
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout,
                       FEME_IMAGE_DEPTH);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  Samp.Flags |= FEME_SAMPLER_COMPARE_ENABLE;
  Samp.CompareFunc = static_cast<uint32_t>(SamplerCompareFunc::GreaterEqual);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCmpCubeFn Fn = resolve<SampleCmpCubeFn>(addWrapper(
      "samplecmp_cube_seamless", "feme.cpu.image.samplecmp.cube.f32"));
  float Out = 9.0f;
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.0f,
     /*DirZ=*/0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true,
     /*Dref=*/0.5f, /*Bias=*/0.0f, -std::numeric_limits<float>::infinity(),
     true, &Out);
  EXPECT_GT(Out, 0.0f) << "expected a nonzero contribution from face 4's "
                          "own passing texel";
  EXPECT_LT(Out, 1.0f) << "expected face 0's own failing taps to still "
                          "contribute (not fully replaced by face 4)";
}

// The doubly-out-of-bounds "corner" case `femeRTRemapCubeEdgeCoords`
// itself flags `Ambiguous` (a bilinear tap whose integer coordinate
// falls outside `[0, Size)` on *both* axes at once, straddling a cube
// corner shared by three faces with no single unique neighbor):
// `femeRTSampleCubeLinearAtLevel`/`femeRTSampleCmpCubeAtLevel` resolve it
// by averaging the other three (non-ambiguous) taps, mirroring
// VK-GL-CTS's own `getCubeLinearSamples`. A direction vector symmetric
// in `Y`/`Z` (both `0.6`, matching `SampleCubeSeamlessBlendsAcrossFaceEdge`'s
// own `Z`) pushes *both* `X0` and `Y0` one texel past face 0's own
// bounds, so the `(X0, Y0)` tap remaps ambiguously; the other three taps
// resolve to three distinct, known faces (0, 2, 4), each given its own
// distinct marker value so the corner tap's own averaged contribution is
// independently verifiable (`(50 + 25 + 100) / 3` -- see the derivation
// in this test's own values below) rather than only checked indirectly
// through the final blended result.
TEST_F(ImageSamplingTest, SampleCubeSeamlessCornerAveragesThreeFaces) {
  float Storage[6][2][2][4];
  for (unsigned Face = 0; Face < 6; ++Face) {
    float Value = Face == 0   ? 100.0f  // Face 0 (+X): T11 (in-bounds).
                  : Face == 2 ? 50.0f   // Face 2 (+Y): T10's own remap.
                  : Face == 4 ? 25.0f   // Face 4 (+Z): T01's own remap.
                              : 0.0f;
    for (unsigned Y = 0; Y < 2; ++Y)
      for (unsigned X = 0; X < 2; ++X)
        for (unsigned C = 0; C < 4; ++C)
          Storage[Face][Y][X][C] = Value;
  }
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2DArray(Storage, sizeof(Storage), 2, 2, 6,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  FemeSamplerDescriptor Samp =
      makeSampler(SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
  FemeSamplerDescriptor SamplerHeap[1] = {Samp};

  SampleCubeFn Fn = resolve<SampleCubeFn>(addWrapper(
      "sample_cube_seamless_corner", "feme.cpu.image.sample.cube.v4f32"));
  // (X=1, Y=0.6, Z=0.6): face 0 (+X) again, but now *both* U and V place
  // their own bilinear footprint one texel past face 0's own bounds --
  // the `(X0, Y0)` tap straddles the corner shared by faces 0, 2, and 4.
  float Out[4];
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, /*DirX=*/1.0f, /*DirY=*/0.6f,
     /*DirZ=*/0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, /*Bias=*/0.0f,
     -std::numeric_limits<float>::infinity(), true, Out);
  // Expected corner value: (face2 (50) + face4 (25) + face0 (100)) / 3
  // ~= 58.33, blended with weight (1 - Wx) * (1 - Wy) ~= 0.01 against the
  // other three known taps (Wx == Wy ~= 0.9, mirroring
  // `SampleCubeSeamlessBlendsAcrossFaceEdge`'s own derivation) -- overall
  // expected result ~= 88.3, distinct from a naive corner-less
  // extrapolation (which would read `100.0`, `0.0`, or some other value
  // entirely if the corner tap were mishandled instead of averaged).
  EXPECT_NEAR(Out[0], 88.3f, 3.0f);
}

// Roadmap H19a: `feme.cpu.image.store.2d.v4f32`/`.v4i32`, the write-side
// counterpart of `feme.cpu.image.load.2d.*` for a plain, non-arrayed,
// non-multisampled storage image.

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR32G32B32A32Float) {
  float Storage[2][2][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 2, 2,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store =
      resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {5.0f, 6.0f, 7.0f, 8.0f};
  Store(ImageHeap, 1, 0, /*X=*/1, /*Y=*/1, Texel, /*Mask=*/true);

  EXPECT_FLOAT_EQ(Storage[1][1][0], 5.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][1], 6.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][2], 7.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][3], 8.0f);
  // Every other texel is untouched.
  EXPECT_FLOAT_EQ(Storage[0][0][0], 0.0f);
}

TEST_F(ImageSamplingTest, StoreWritesOnlyRedComponentIntoR32Float) {
  // `R32_FLOAT` is a single-component format: only the texel's own first
  // 4 bytes are written, matching `femeRTPackImageTexel`'s R32_FLOAT case.
  float Storage[1][1] = {{-1.0f}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32_FLOAT, Layout,
      FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store =
      resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {42.0f, 99.0f, 99.0f, 99.0f};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_FLOAT_EQ(Storage[0][0], 42.0f);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR32G32B32A32Uint) {
  uint32_t Storage[1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_UINT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {10, 20, 30, 40};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 10u);
  EXPECT_EQ(Storage[0][0][1], 20u);
  EXPECT_EQ(Storage[0][0][2], 30u);
  EXPECT_EQ(Storage[0][0][3], 40u);
}

TEST_F(ImageSamplingTest, StoreWritesOnlyRedComponentIntoR32Sint) {
  int32_t Storage[1][1] = {{-1}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R32_SINT, Layout,
      FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {-7, 0, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], -7);
}

// Roadmap H19f: `R16G16B16A16_{SFLOAT,UINT,SINT}` widen
// `femeRTPackImageTexel`/`femeRTPackImageTexelI32` past the mandatory
// storage-image format floor -- a first slice of the full
// `shaderStorageImageExtendedFormats` list.

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16B16A16FloatViaHalfEncode) {
  uint16_t Storage[1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R16G16B16A16_FLOAT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store =
      resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {1.0f, -2.5f, 0.0f, 65504.0f}; // Last: half's max.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);

  // 1.0f -> 0x3C00, -2.5f -> 0xC100, 0.0f -> 0x0000, 65504.0f -> 0x7BFF,
  // the standard binary16 bit patterns for each value.
  EXPECT_EQ(Storage[0][0][0], 0x3C00u);
  EXPECT_EQ(Storage[0][0][1], 0xC100u);
  EXPECT_EQ(Storage[0][0][2], 0x0000u);
  EXPECT_EQ(Storage[0][0][3], 0x7BFFu);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16B16A16Uint) {
  uint16_t Storage[1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R16G16B16A16_UINT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  // 0x10203 truncates to 0x0203 -- confirms the store truncates to 16
  // bits rather than clamping or wrapping some other way.
  FeMeTestV4I32 Texel = {0x10203, 65535, 0, 1};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 0x0203u);
  EXPECT_EQ(Storage[0][0][1], 65535u);
  EXPECT_EQ(Storage[0][0][2], 0u);
  EXPECT_EQ(Storage[0][0][3], 1u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16B16A16Sint) {
  int16_t Storage[1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R16G16B16A16_SINT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {-1, -32768, 32767, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], -1);
  EXPECT_EQ(Storage[0][0][1], -32768);
  EXPECT_EQ(Storage[0][0][2], 32767);
  EXPECT_EQ(Storage[0][0][3], 0);
}

// Roadmap H19h: `R16G16B16A16_{UNORM,SNORM}` widen
// `femeRTPackImageTexel`/`femeRTUnpackImageTexel` further past the
// mandatory storage-image format floor, alongside a new sampled-image
// (`VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT`) advertisement for the same two
// formats -- previously entirely unimplemented (not even readable) at
// the runtime level, unlike H19f's `_SFLOAT`/`_UINT`/`_SINT` siblings.

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16B16A16UnormQuantized) {
  uint16_t Storage[1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R16G16B16A16_UNORM, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {1.0f, 0.0f, 0.5f, 2.0f}; // Last: out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 65535u);
  EXPECT_EQ(Storage[0][0][1], 0u);
  EXPECT_NEAR(Storage[0][0][2], 32768u, 1u);
  EXPECT_EQ(Storage[0][0][3], 65535u); // Clamped to 1.0 before quantizing.
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16B16A16SnormQuantized) {
  int16_t Storage[1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R16G16B16A16_SNORM, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {1.0f, -1.0f, 0.0f, -2.0f}; // Last: out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 32767);
  EXPECT_EQ(Storage[0][0][1], -32767);
  EXPECT_EQ(Storage[0][0][2], 0);
  EXPECT_EQ(Storage[0][0][3], -32767); // Clamped to -1.0 before quantizing.
}

// Roadmap H19j: `R8_{UNORM,SNORM,UINT,SINT}` widen
// `femeRTPackImageTexel`/`femeRTPackImageTexelI32` further past the
// mandatory storage-image format floor, the single-channel siblings of
// H19h's `R16G16B16A16_{UNORM,SNORM}` widening above. Only the first
// component is ever stored, matching `R32_FLOAT`/`R32_UINT`/`_SINT`'s own
// single-component storage-image convention.

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8UnormQuantized) {
  uint8_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_UNORM, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {2.0f, 0.0f, 0.0f, 0.0f}; // Out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], 255u); // Clamped to 1.0 before quantizing.
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8SnormQuantized) {
  int8_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_SNORM, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {-2.0f, 0.0f, 0.0f, 0.0f}; // Out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], -127); // Clamped to -1.0 before quantizing.
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8Uint) {
  uint8_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_UINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  // 0x102 truncates to 0x02 -- confirms the store truncates to 8 bits.
  FeMeTestV4I32 Texel = {0x102, 0, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], 0x02u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8Sint) {
  int8_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_SINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {-1, 0, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], -1);
}

/// Clang gives an `asm("...")`-labelled runtime entry point a leading
/// `GlobalValue::dropLLVMManglingEscape` ('\01') byte in its IR name on any
/// target with a non-empty global prefix (Darwin's '_'), but not on ELF.
/// Resolving such a name has to work regardless: rename one runtime entry
/// point to its escaped spelling and confirm it still resolves and runs.
TEST_F(ImageSamplingTest, ResolvesRuntimeFunctionWithManglingEscape) {
  Function *F = getRuntimeFunction(*M, "feme.cpu.image.store.2d.v4i32");
  ASSERT_NE(F, nullptr);
  if (!F->getName().starts_with("\1"))
    F->setName("\1feme.cpu.image.store.2d.v4i32");
  ASSERT_TRUE(F->getName().starts_with("\1"));

  int8_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8_SINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store = resolve<StoreI32Fn>(F);
  FeMeTestV4I32 Texel = {-1, 0, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], -1);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8G8UnormQuantized) {
  uint8_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8G8_UNORM, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {2.0f, 0.5f, 0.0f, 0.0f}; // R out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 255u); // Clamped to 1.0 before quantizing.
  EXPECT_EQ(Storage[0][0][1], 128u); // round(0.5 * 255) == 128.
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8G8SnormQuantized) {
  int8_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8G8_SNORM, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {-2.0f, 0.5f, 0.0f, 0.0f}; // R out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], -127); // Clamped to -1.0 before quantizing.
  EXPECT_EQ(Storage[0][0][1], 64);   // round(0.5 * 127) == 64.
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8G8Uint) {
  uint8_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8G8_UINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  // 0x102 truncates to 0x02 -- confirms the store truncates to 8 bits.
  FeMeTestV4I32 Texel = {0x102, 0x203, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 0x02u);
  EXPECT_EQ(Storage[0][0][1], 0x03u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8G8Sint) {
  int8_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R8G8_SINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {-1, -2, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], -1);
  EXPECT_EQ(Storage[0][0][1], -2);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16FloatViaHalfEncode) {
  uint16_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_FLOAT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {-2.5f, 0.0f, 0.0f, 0.0f};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  // -2.5f -> 0xC100, the standard binary16 bit pattern, matching
  // R16G16B16A16_FLOAT's own precedent.
  EXPECT_EQ(Storage[0][0], 0xC100u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16UnormQuantized) {
  uint16_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_UNORM, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {2.0f, 0.0f, 0.0f, 0.0f}; // Out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], 65535u); // Clamped to 1.0 before quantizing.
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16SnormQuantized) {
  uint16_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_SNORM, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {-2.0f, 0.0f, 0.0f, 0.0f}; // Out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ((int16_t)Storage[0][0], -32767); // Clamped to -1.0.
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16Uint) {
  uint16_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_UINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  // 0x10002 truncates to 0x0002 -- confirms the store truncates to 16 bits.
  FeMeTestV4I32 Texel = {0x10002, 0, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], 0x0002u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16Sint) {
  uint16_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16_SINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {-1, 0, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ((int16_t)Storage[0][0], -1);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16FloatViaHalfEncode) {
  uint16_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_FLOAT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {1.0f, -2.5f, 0.0f, 0.0f};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  // 1.0f -> 0x3C00, -2.5f -> 0xC100, the standard binary16 bit patterns,
  // matching R16G16B16A16_FLOAT's own precedent.
  EXPECT_EQ(Storage[0][0][0], 0x3C00u);
  EXPECT_EQ(Storage[0][0][1], 0xC100u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16UnormQuantized) {
  uint16_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_UNORM, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {2.0f, 0.5f, 0.0f, 0.0f}; // R out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 65535u); // Clamped to 1.0 before quantizing.
  EXPECT_EQ(Storage[0][0][1], 32768u); // round(0.5 * 65535) == 32768.
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16SnormQuantized) {
  uint16_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_SNORM, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {-2.0f, 0.5f, 0.0f, 0.0f}; // R out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ((int16_t)Storage[0][0][0], -32767); // Clamped to -1.0.
  EXPECT_EQ((int16_t)Storage[0][0][1], 16384);  // round(0.5 * 32767).
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16Uint) {
  uint16_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_UINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  // 0x10002 truncates to 0x0002 -- confirms the store truncates to 16
  // bits.
  FeMeTestV4I32 Texel = {0x10002, 0x20003, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 0x0002u);
  EXPECT_EQ(Storage[0][0][1], 0x0003u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR16G16Sint) {
  uint16_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R16G16_SINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {-1, -2, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ((int16_t)Storage[0][0][0], -1);
  EXPECT_EQ((int16_t)Storage[0][0][1], -2);
}

// (Roadmap H19n) `R32G32_UINT`/`_SINT`: identity format, no truncation --
// confirms only the first two lanes are stored, unlike
// `R32G32B32A32_{UINT,SINT}`'s own four-lane identity store.
TEST_F(ImageSamplingTest, StoreWritesTexelIntoR32G32Uint) {
  uint32_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R32G32_UINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {(int32_t)4000000000u, 128, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], 4000000000u);
  EXPECT_EQ(Storage[0][0][1], 128u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR32G32Sint) {
  int32_t Storage[1][1][2] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(Storage, sizeof(Storage), 1, 1,
                                        ResourceFormat::R32G32_SINT, Layout,
                                        FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {-1, -2, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], -1);
  EXPECT_EQ(Storage[0][0][1], -2);
}

// (Roadmap H19n) The packed 32-bit formats
// `A2B10G10R10_{UNORM,UINT}_PACK32`/`B10G11R11_UFLOAT_PACK32`: each pack
// helper is the mathematical inverse of this project's own existing
// sampled-image unpack helper for the same format.
TEST_F(ImageSamplingTest, StoreWritesTexelIntoR10G10B10A2UnormQuantized) {
  uint32_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R10G10B10A2_UNORM, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  // R out-of-range (clamps to 1.0 -> 1023); G = 0.0 -> 0; B = 0.5 ->
  // round(0.5 * 1023) == 512 (10-bit fields); A = 1.0 -> round(1.0 * 3)
  // == 3 (2-bit field).
  FeMeTestV4F32 Texel = {2.0f, 0.0f, 0.5f, 1.0f};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  uint32_t Raw = Storage[0][0];
  EXPECT_EQ(Raw & 0x3FFu, 1023u);
  EXPECT_EQ((Raw >> 10) & 0x3FFu, 0u);
  EXPECT_EQ((Raw >> 20) & 0x3FFu, 512u);
  EXPECT_EQ((Raw >> 30) & 0x3u, 3u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR11G11B10FloatViaHalfEncode) {
  uint32_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2D(
      Storage, sizeof(Storage), 1, 1, ResourceFormat::R11G11B10_FLOAT,
      Layout, FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  // R=1.0, G=2.0, B=1.5, matching `LoadFetchesR11G11B10FloatNonZeroValues`
  // above -- confirms the round trip in the opposite direction.
  FeMeTestV4F32 Texel = {1.0f, 2.0f, 1.5f, 0.0f};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  uint32_t Raw = Storage[0][0];
  uint32_t RRaw = Raw & 0x7FFu;
  uint32_t GRaw = (Raw >> 11) & 0x7FFu;
  uint32_t BRaw = (Raw >> 22) & 0x3FFu;
  EXPECT_EQ(RRaw, (15u << 6) | 0u);
  EXPECT_EQ(GRaw, (16u << 6) | 0u);
  EXPECT_EQ(BRaw, (15u << 5) | 16u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR10G10B10A2Uint) {
  uint32_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R10G10B10A2_UINT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  // 0x400 (1024) truncates to 0 in a 10-bit field -- confirms the store
  // truncates rather than clamping.
  FeMeTestV4I32 Texel = {0x400, 512, 256, 1};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  uint32_t Raw = Storage[0][0];
  EXPECT_EQ(Raw & 0x3FFu, 0u);
  EXPECT_EQ((Raw >> 10) & 0x3FFu, 512u);
  EXPECT_EQ((Raw >> 20) & 0x3FFu, 256u);
  EXPECT_EQ((Raw >> 30) & 0x3u, 1u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR10G10B10A2Snorm) {
  // Roadmap H19o: the signed-normalized sibling of
  // `StoreWritesTexelIntoR10G10B10A2UnormQuantized` above. R out-of-range
  // (clamps to 1.0 -> round(1.0 * 511) == 511); G = -1.0 -> round(-1.0 *
  // 511) == -511, whose 10-bit two's-complement bit pattern is 0x201; B =
  // 0.0 -> 0; A = -1.0 -> round(-1.0 * 1) == -1, whose 2-bit two's-
  // complement bit pattern is 0b11.
  uint32_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R10G10B10A2_SNORM, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {2.0f, -1.0f, 0.0f, -1.0f};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  uint32_t Raw = Storage[0][0];
  EXPECT_EQ(Raw & 0x3FFu, 511u);
  EXPECT_EQ((Raw >> 10) & 0x3FFu, 0x201u);
  EXPECT_EQ((Raw >> 20) & 0x3FFu, 0u);
  EXPECT_EQ((Raw >> 30) & 0x3u, 0x3u);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR10G10B10A2Sint) {
  // Roadmap H19o: confirms the pack side is genuinely shared with
  // `R10G10B10A2_UINT` (a negative signed lane truncates to the same bit
  // pattern a large-enough unsigned lane would) -- -512 truncates to
  // 0x200 in the 10-bit B field, and -1 truncates to 0b11 in the 2-bit A
  // field, the same raw bit patterns
  // `LoadI32FetchesR10G10B10A2Sint`'s own unpack side reads back as -512
  // and -1 respectively.
  uint32_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R10G10B10A2_SINT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {1, 256, -512, -1};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  uint32_t Raw = Storage[0][0];
  EXPECT_EQ(Raw & 0x3FFu, 1u);
  EXPECT_EQ((Raw >> 10) & 0x3FFu, 256u);
  EXPECT_EQ((Raw >> 20) & 0x3FFu, 0x200u);
  EXPECT_EQ((Raw >> 30) & 0x3u, 0x3u);
}

// (Roadmap H19n) `R8G8B8A8_SNORM`/`_SINT`: a real mandatory
// `shaderStorageImageExtendedFormats` entry discovered via the Vulkan
// spec's own full mandatory list -- reuses `femeRTPackR8G8B8A8Snorm`/
// `Sint`, already defined for this project's own texel-buffer conversion
// path.
TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8G8B8A8SnormQuantized) {
  uint32_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R8G8B8A8_SNORM, Layout, FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store = resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {2.0f, -1.0f, 0.5f, 0.0f}; // R out-of-range, clamps.
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  uint32_t Raw = Storage[0][0];
  EXPECT_EQ((int8_t)(Raw & 0xFFu), 127); // Clamped to 1.0 before quantizing.
  EXPECT_EQ((int8_t)((Raw >> 8) & 0xFFu), -127);
  EXPECT_EQ((int8_t)((Raw >> 16) & 0xFFu), 64); // round(0.5 * 127) == 64.
  EXPECT_EQ((int8_t)((Raw >> 24) & 0xFFu), 0);
}

TEST_F(ImageSamplingTest, StoreWritesTexelIntoR8G8B8A8Sint) {
  uint32_t Storage[1][1] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R8G8B8A8_SINT, Layout, FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreI32Fn Store =
      resolveRuntime<StoreI32Fn>("feme.cpu.image.store.2d.v4i32");
  FeMeTestV4I32 Texel = {-1, -2, 3, 0};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/true);
  uint32_t Raw = Storage[0][0];
  EXPECT_EQ((int8_t)(Raw & 0xFFu), -1);
  EXPECT_EQ((int8_t)((Raw >> 8) & 0xFFu), -2);
  EXPECT_EQ((int8_t)((Raw >> 16) & 0xFFu), 3);
  EXPECT_EQ((int8_t)((Raw >> 24) & 0xFFu), 0);
}

TEST_F(ImageSamplingTest, StoreOutOfBoundsCoordinateIsANoOp) {
  float Storage[1][1][4] = {{{1, 2, 3, 4}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store =
      resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(ImageHeap, 1, 0, /*X=*/5, /*Y=*/5, Texel, /*Mask=*/true);
  EXPECT_FLOAT_EQ(Storage[0][0][0], 1.0f);
  EXPECT_FLOAT_EQ(Storage[0][0][1], 2.0f);
}

TEST_F(ImageSamplingTest, InactiveLaneStoreIsANoOp) {
  float Storage[1][1][4] = {{{1, 2, 3, 4}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage2D(Storage, sizeof(Storage), 1, 1,
                  ResourceFormat::R32G32B32A32_FLOAT, Layout,
                  FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreFn Store =
      resolveRuntime<StoreFn>("feme.cpu.image.store.2d.v4f32");
  FeMeTestV4F32 Texel = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(ImageHeap, 1, 0, 0, 0, Texel, /*Mask=*/false);
  EXPECT_FLOAT_EQ(Storage[0][0][0], 1.0f);
}

// Roadmap H19b: `feme.cpu.image.store.2darray.v4f32`/`.v4i32`, the arrayed
// counterpart of `feme.cpu.image.store.2d.*` above.

TEST_F(ImageSamplingTest, StoreArrayWritesTexelIntoTheAddressedLayerOnly) {
  float Storage[2][2][2][4] = {}; // [Layer][Y][X][Channel].
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2DArray(
      Storage, sizeof(Storage), 2, 2, /*ArrayLayers=*/2,
      ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreArrayFn Store =
      resolveRuntime<StoreArrayFn>("feme.cpu.image.store.2darray.v4f32");
  FeMeTestV4F32 Texel = {5.0f, 6.0f, 7.0f, 8.0f};
  Store(ImageHeap, 1, 0, /*X=*/1, /*Y=*/1, /*Layer=*/1, Texel, /*Mask=*/true);

  EXPECT_FLOAT_EQ(Storage[1][1][1][0], 5.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][1][1], 6.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][1][2], 7.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][1][3], 8.0f);
  // Layer 0's identical (X, Y) is untouched -- confirms the write actually
  // addresses one layer, not every layer.
  EXPECT_FLOAT_EQ(Storage[0][1][1][0], 0.0f);
}

TEST_F(ImageSamplingTest, StoreArrayWritesTexelIntoR32G32B32A32Uint) {
  uint32_t Storage[2][1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2DArray(
      Storage, sizeof(Storage), 1, 1, /*ArrayLayers=*/2,
      ResourceFormat::R32G32B32A32_UINT, Layout, FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreArrayI32Fn Store = resolveRuntime<StoreArrayI32Fn>(
      "feme.cpu.image.store.2darray.v4i32");
  FeMeTestV4I32 Texel = {10, 20, 30, 40};
  Store(ImageHeap, 1, 0, 0, 0, /*Layer=*/1, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[1][0][0][0], 10u);
  EXPECT_EQ(Storage[1][0][0][3], 40u);
  EXPECT_EQ(Storage[0][0][0][0], 0u);
}

TEST_F(ImageSamplingTest, StoreArrayOutOfBoundsLayerIsANoOp) {
  float Storage[1][1][1][4] = {{{{1, 2, 3, 4}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage2DArray(
      Storage, sizeof(Storage), 1, 1, /*ArrayLayers=*/1,
      ResourceFormat::R32G32B32A32_FLOAT, Layout, FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreArrayFn Store =
      resolveRuntime<StoreArrayFn>("feme.cpu.image.store.2darray.v4f32");
  FeMeTestV4F32 Texel = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(ImageHeap, 1, 0, 0, 0, /*Layer=*/5, Texel, /*Mask=*/true);
  EXPECT_FLOAT_EQ(Storage[0][0][0][0], 1.0f);
}

// Roadmap H19g: `feme.cpu.image.store.2dms.v4f32`/`.v4i32`, the plain
// (non-arrayed) multisampled counterpart of `feme.cpu.image.store.2d.*`
// above -- addresses one sample of one texel, mirroring
// `LoadFetchesExplicitSampleOfMultisampledTexel`'s own manually-built
// multisample layout (`SampleStride`) on the write side.

TEST_F(ImageSamplingTest, StoreMSWritesTexelIntoTheAddressedSampleOnly) {
  // A 1x1, 4-sample R32G32B32A32_FLOAT storage image: each sample's own
  // 4-channel texel is stored contiguously (`SampleStride ==
  // 4 * sizeof(float)`, one channel-quad per sample).
  float Storage[4][4] = {}; // [Sample][Channel].
  FemeImageSubresourceLayout Layout{};
  Layout.RowPitch = 4 * 4 * sizeof(float);
  Layout.SlicePitch = Layout.RowPitch;
  Layout.SampleStride = 4 * sizeof(float);
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 4;
  Img.Flags = FEME_IMAGE_STORAGE;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreMSFn Store =
      resolveRuntime<StoreMSFn>("feme.cpu.image.store.2dms.v4f32");
  FeMeTestV4F32 Texel = {5.0f, 6.0f, 7.0f, 8.0f};
  Store(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Sample=*/2, Texel,
       /*Mask=*/true);

  EXPECT_FLOAT_EQ(Storage[2][0], 5.0f);
  EXPECT_FLOAT_EQ(Storage[2][1], 6.0f);
  EXPECT_FLOAT_EQ(Storage[2][2], 7.0f);
  EXPECT_FLOAT_EQ(Storage[2][3], 8.0f);
  // Sample 0's identical (X, Y) is untouched -- confirms the write
  // actually addresses one sample, not every sample.
  EXPECT_FLOAT_EQ(Storage[0][0], 0.0f);
}

TEST_F(ImageSamplingTest, StoreMSWritesTexelIntoR32G32B32A32Uint) {
  uint32_t Storage[2][4] = {}; // [Sample][Channel].
  FemeImageSubresourceLayout Layout{};
  Layout.RowPitch = 2 * 4 * sizeof(uint32_t);
  Layout.SlicePitch = Layout.RowPitch;
  Layout.SampleStride = 4 * sizeof(uint32_t);
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_UINT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 2;
  Img.Flags = FEME_IMAGE_STORAGE;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreMSI32Fn Store =
      resolveRuntime<StoreMSI32Fn>("feme.cpu.image.store.2dms.v4i32");
  FeMeTestV4I32 Texel = {10, 20, 30, 40};
  Store(ImageHeap, 1, 0, 0, 0, /*Sample=*/1, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[1][0], 10u);
  EXPECT_EQ(Storage[1][3], 40u);
  EXPECT_EQ(Storage[0][0], 0u);
}

TEST_F(ImageSamplingTest, StoreMSOutOfBoundsSampleIsANoOp) {
  float Storage[1][4] = {{1, 2, 3, 4}};
  FemeImageSubresourceLayout Layout{};
  Layout.RowPitch = 1 * 4 * sizeof(float);
  Layout.SlicePitch = Layout.RowPitch;
  Layout.SampleStride = 4 * sizeof(float);
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2D);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_STORAGE;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreMSFn Store =
      resolveRuntime<StoreMSFn>("feme.cpu.image.store.2dms.v4f32");
  FeMeTestV4F32 Texel = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(ImageHeap, 1, 0, 0, 0, /*Sample=*/5, Texel, /*Mask=*/true);
  EXPECT_FLOAT_EQ(Storage[0][0], 1.0f);
}

// Roadmap H19m: `feme.cpu.image.store.2darrayms.v4f32`/`.v4i32`, the
// arrayed-*and*-multisampled counterpart of `feme.cpu.image.store.2d.*`
// above -- combines `StoreArrayFn`'s own per-layer addressing and
// `StoreMSFn`'s own per-sample addressing in one write, the last row
// closing out `shaderStorageImageMultisample`'s own remaining gap. Like
// `StoreMSWritesTexelIntoTheAddressedSampleOnly` above, `makeImage2DArray`
// does not support `SampleCount > 1`, so the descriptor/layout is built by
// hand here too.

TEST_F(ImageSamplingTest,
       StoreArrayMSWritesTexelIntoTheAddressedLayerAndSampleOnly) {
  // A 1x1, 2-layer, 2-sample R32G32B32A32_FLOAT storage image: each
  // layer's own samples are stored contiguously (`SlicePitch ==
  // SampleCount * SampleStride`), each sample's own 4-channel texel
  // likewise contiguous (`SampleStride == 4 * sizeof(float)`).
  float Storage[2][2][4] = {}; // [Layer][Sample][Channel].
  FemeImageSubresourceLayout Layout{};
  Layout.SampleStride = 4 * sizeof(float);
  Layout.RowPitch = 2 * Layout.SampleStride;
  Layout.SlicePitch = Layout.RowPitch;
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2DArray);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 2;
  Img.PlaneCount = 1;
  Img.SampleCount = 2;
  Img.Flags = FEME_IMAGE_STORAGE;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreArrayMSFn Store = resolveRuntime<StoreArrayMSFn>(
      "feme.cpu.image.store.2darrayms.v4f32");
  FeMeTestV4F32 Texel = {5.0f, 6.0f, 7.0f, 8.0f};
  Store(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Layer=*/1, /*Sample=*/1, Texel,
       /*Mask=*/true);

  EXPECT_FLOAT_EQ(Storage[1][1][0], 5.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][3], 8.0f);
  // Every other (layer, sample) combination is untouched -- confirms the
  // write actually addresses exactly one (layer, sample) pair, not every
  // sample of the layer or every layer of the sample.
  EXPECT_FLOAT_EQ(Storage[1][0][0], 0.0f);
  EXPECT_FLOAT_EQ(Storage[0][1][0], 0.0f);
  EXPECT_FLOAT_EQ(Storage[0][0][0], 0.0f);
}

TEST_F(ImageSamplingTest, StoreArrayMSWritesTexelIntoR32G32B32A32Uint) {
  uint32_t Storage[2][2][4] = {}; // [Layer][Sample][Channel].
  FemeImageSubresourceLayout Layout{};
  Layout.SampleStride = 4 * sizeof(uint32_t);
  Layout.RowPitch = 2 * Layout.SampleStride;
  Layout.SlicePitch = Layout.RowPitch;
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2DArray);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_UINT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 2;
  Img.PlaneCount = 1;
  Img.SampleCount = 2;
  Img.Flags = FEME_IMAGE_STORAGE;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreArrayMSI32Fn Store = resolveRuntime<StoreArrayMSI32Fn>(
      "feme.cpu.image.store.2darrayms.v4i32");
  FeMeTestV4I32 Texel = {10, 20, 30, 40};
  Store(ImageHeap, 1, 0, 0, 0, /*Layer=*/1, /*Sample=*/0, Texel,
       /*Mask=*/true);
  EXPECT_EQ(Storage[1][0][0], 10u);
  EXPECT_EQ(Storage[1][0][3], 40u);
  EXPECT_EQ(Storage[0][0][0], 0u);
}

TEST_F(ImageSamplingTest, StoreArrayMSOutOfBoundsLayerOrSampleIsANoOp) {
  float Storage[1][1][4] = {{{1, 2, 3, 4}}};
  FemeImageSubresourceLayout Layout{};
  Layout.SampleStride = 4 * sizeof(float);
  Layout.RowPitch = 1 * Layout.SampleStride;
  Layout.SlicePitch = Layout.RowPitch;
  FemeImageDescriptor Img{};
  Img.Data = Storage;
  Img.SizeInBytes = sizeof(Storage);
  Img.Dimension = static_cast<uint32_t>(ImageDimension::Texture2DArray);
  Img.Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Img.Width = 1;
  Img.Height = 1;
  Img.Depth = 1;
  Img.MipLevels = 1;
  Img.ArrayLayers = 1;
  Img.PlaneCount = 1;
  Img.SampleCount = 1;
  Img.Flags = FEME_IMAGE_STORAGE;
  Img.MipLayouts = &Layout;
  Img.MipLayoutCount = 1;
  FemeImageDescriptor ImageHeap[1] = {Img};

  StoreArrayMSFn Store = resolveRuntime<StoreArrayMSFn>(
      "feme.cpu.image.store.2darrayms.v4f32");
  FeMeTestV4F32 Texel = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(ImageHeap, 1, 0, 0, 0, /*Layer=*/3, /*Sample=*/0, Texel,
       /*Mask=*/true);
  Store(ImageHeap, 1, 0, 0, 0, /*Layer=*/0, /*Sample=*/3, Texel,
       /*Mask=*/true);
  EXPECT_FLOAT_EQ(Storage[0][0][0], 1.0f);
}

// Roadmap H19c: `feme.cpu.image.load.1d.v4f32`/`.v4i32`/
// `feme.cpu.image.store.1d.v4f32`/`.v4i32`, the plain-1D counterparts of
// the plain-2D load/store pair above.

TEST_F(ImageSamplingTest, Load1DFetchesTexelAtX) {
  float Storage[3][4] = {{1, 2, 3, 4}, {5, 6, 7, 8}, {9, 10, 11, 12}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1D(
      Storage, sizeof(Storage), 3, ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load1DFn Fn =
      resolve<Load1DFn>(addWrapper("load1d", "feme.cpu.image.load.1d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/1, /*Mip=*/0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 5.0f);
  EXPECT_FLOAT_EQ(Out[1], 6.0f);
  EXPECT_FLOAT_EQ(Out[2], 7.0f);
  EXPECT_FLOAT_EQ(Out[3], 8.0f);
}

TEST_F(ImageSamplingTest, Load1DOutOfBoundsXReadsZero) {
  float Storage[1][4] = {{1, 2, 3, 4}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1D(
      Storage, sizeof(Storage), 1, ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load1DFn Fn =
      resolve<Load1DFn>(addWrapper("load1d", "feme.cpu.image.load.1d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/5, /*Mip=*/0, /*Sample=*/0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 0.0f);
}

TEST_F(ImageSamplingTest, Load1DI32FetchesIntegerTexel) {
  int32_t Storage[2][4] = {{-1, -2, -3, -4}, {10, 20, 30, 40}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage1D(
      Storage, sizeof(Storage), 2, ResourceFormat::R32G32B32A32_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load1DI32Fn Fn = resolve<Load1DI32Fn>(
      addWrapper("load1di32", "feme.cpu.image.load.1d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/1, /*Mip=*/0, true, Out);
  EXPECT_EQ(Out[0], 10);
  EXPECT_EQ(Out[3], 40);
}

TEST_F(ImageSamplingTest, Store1DWritesTexelAtX) {
  float Storage[2][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1D(Storage, sizeof(Storage), 2,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout,
                 FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store1DFn Store = resolveRuntime<Store1DFn>("feme.cpu.image.store.1d.v4f32");
  FeMeTestV4F32 Texel = {5.0f, 6.0f, 7.0f, 8.0f};
  Store(ImageHeap, 1, 0, /*X=*/1, Texel, /*Mask=*/true);

  EXPECT_FLOAT_EQ(Storage[1][0], 5.0f);
  EXPECT_FLOAT_EQ(Storage[1][3], 8.0f);
  // Every other texel is untouched.
  EXPECT_FLOAT_EQ(Storage[0][0], 0.0f);
}

TEST_F(ImageSamplingTest, Store1DI32WritesIntegerTexel) {
  int32_t Storage[1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1D(Storage, sizeof(Storage), 1,
                 ResourceFormat::R32G32B32A32_SINT, Layout,
                 FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store1DI32Fn Store =
      resolveRuntime<Store1DI32Fn>("feme.cpu.image.store.1d.v4i32");
  FeMeTestV4I32 Texel = {-7, 0, 0, 0};
  Store(ImageHeap, 1, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0], -7);
}

TEST_F(ImageSamplingTest, Store1DOutOfBoundsXIsANoOp) {
  float Storage[1][4] = {{1, 2, 3, 4}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1D(Storage, sizeof(Storage), 1,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout,
                 FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store1DFn Store = resolveRuntime<Store1DFn>("feme.cpu.image.store.1d.v4f32");
  FeMeTestV4F32 Texel = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(ImageHeap, 1, 0, /*X=*/5, Texel, /*Mask=*/true);
  EXPECT_FLOAT_EQ(Storage[0][0], 1.0f);
}

// Roadmap H19e: `feme.cpu.image.load.1darray.v4f32`/`.v4i32`/
// `feme.cpu.image.store.1darray.v4f32`/`.v4i32`, the arrayed-1D
// counterparts of the plain-1D load/store pair above -- the one dimension
// left out of both H19b's own array scope (`Texture2DArray` only) and
// H19c's own non-arrayed scope (`Texture1D`/`Texture3D` only).

TEST_F(ImageSamplingTest, Load1DArrayReadsRequestedLayer) {
  // Storage[layer][x][channel].
  float Storage[2][3][4] = {{{1, 1, 1, 1}, {2, 2, 2, 2}, {3, 3, 3, 3}},
                            {{4, 4, 4, 4}, {5, 5, 5, 5}, {6, 6, 6, 6}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1DArray(Storage, sizeof(Storage), 3, 2,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load1DArrayFn Fn = resolve<Load1DArrayFn>(
      addWrapper("load1darray", "feme.cpu.image.load.1darray.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/1, /*Layer=*/1, /*Mip=*/0, /*Sample=*/0, true,
     Out);
  EXPECT_FLOAT_EQ(Out[0], 5.0f);
  EXPECT_FLOAT_EQ(Out[3], 5.0f);
}

TEST_F(ImageSamplingTest, Load1DArrayOutOfRangeLayerReadsZero) {
  float Storage[1][2][4] = {{{1, 1, 1, 1}, {2, 2, 2, 2}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1DArray(Storage, sizeof(Storage), 2, 1,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load1DArrayFn Fn = resolve<Load1DArrayFn>(
      addWrapper("load1darray", "feme.cpu.image.load.1darray.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Layer=*/5, /*Mip=*/0, /*Sample=*/0, true,
     Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 0.0f);
}

TEST_F(ImageSamplingTest, Load1DArrayI32ReadsRequestedLayer) {
  int32_t Storage[2][1][4] = {{{-1, -2, -3, -4}}, {{10, 20, 30, 40}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1DArray(Storage, sizeof(Storage), 1, 2,
                       ResourceFormat::R32G32B32A32_SINT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load1DArrayI32Fn Fn = resolve<Load1DArrayI32Fn>(
      addWrapper("load1darrayi32", "feme.cpu.image.load.1darray.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Layer=*/1, /*Mip=*/0, true, Out);
  EXPECT_EQ(Out[0], 10);
  EXPECT_EQ(Out[3], 40);
}

TEST_F(ImageSamplingTest, Store1DArrayWritesRequestedLayer) {
  float Storage[2][2][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1DArray(Storage, sizeof(Storage), 2, 2,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout,
                       FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store1DArrayFn Store =
      resolveRuntime<Store1DArrayFn>("feme.cpu.image.store.1darray.v4f32");
  FeMeTestV4F32 Texel = {5.0f, 6.0f, 7.0f, 8.0f};
  Store(ImageHeap, 1, 0, /*X=*/1, /*Layer=*/1, Texel, /*Mask=*/true);

  EXPECT_FLOAT_EQ(Storage[1][1][0], 5.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][3], 8.0f);
  // Every other texel is untouched.
  EXPECT_FLOAT_EQ(Storage[0][0][0], 0.0f);
  EXPECT_FLOAT_EQ(Storage[0][1][0], 0.0f);
  EXPECT_FLOAT_EQ(Storage[1][0][0], 0.0f);
}

TEST_F(ImageSamplingTest, Store1DArrayI32WritesIntegerTexel) {
  int32_t Storage[1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1DArray(Storage, sizeof(Storage), 1, 1,
                       ResourceFormat::R32G32B32A32_SINT, Layout,
                       FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store1DArrayI32Fn Store =
      resolveRuntime<Store1DArrayI32Fn>("feme.cpu.image.store.1darray.v4i32");
  FeMeTestV4I32 Texel = {-7, 0, 0, 0};
  Store(ImageHeap, 1, 0, /*X=*/0, /*Layer=*/0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0], -7);
}

TEST_F(ImageSamplingTest, Store1DArrayOutOfRangeLayerIsANoOp) {
  float Storage[1][1][4] = {{{1, 2, 3, 4}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage1DArray(Storage, sizeof(Storage), 1, 1,
                       ResourceFormat::R32G32B32A32_FLOAT, Layout,
                       FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store1DArrayFn Store =
      resolveRuntime<Store1DArrayFn>("feme.cpu.image.store.1darray.v4f32");
  FeMeTestV4F32 Texel = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(ImageHeap, 1, 0, /*X=*/0, /*Layer=*/5, Texel, /*Mask=*/true);
  EXPECT_FLOAT_EQ(Storage[0][0][0], 1.0f);
}

// Roadmap H19c: `feme.cpu.image.load.3d.v4f32`/`.v4i32`/
// `feme.cpu.image.store.3d.v4f32`/`.v4i32`, the plain-3D counterparts,
// addressing a real depth slice via `SlicePitch` (like an array layer)
// but bounds-checked against `Img.Depth`, not `Img.ArrayLayers`.

TEST_F(ImageSamplingTest, Load3DFetchesTexelAtXYZ) {
  float Storage[2][2][2][4] = {}; // [Z][Y][X][Channel].
  Storage[1][1][0][0] = 42.0f;
  Storage[1][1][0][1] = 43.0f;
  Storage[1][1][0][2] = 44.0f;
  Storage[1][1][0][3] = 45.0f;
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage3D(Storage, sizeof(Storage), 2, 2, 2,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load3DFn Fn =
      resolve<Load3DFn>(addWrapper("load3d", "feme.cpu.image.load.3d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Y=*/1, /*Z=*/1, /*Mip=*/0, /*Sample=*/0, true,
     Out);
  EXPECT_FLOAT_EQ(Out[0], 42.0f);
  EXPECT_FLOAT_EQ(Out[1], 43.0f);
  EXPECT_FLOAT_EQ(Out[2], 44.0f);
  EXPECT_FLOAT_EQ(Out[3], 45.0f);
  // A different (X, Y, Z) reads the untouched zero texel.
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Z=*/0, /*Mip=*/0, /*Sample=*/0, true,
     Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
}

TEST_F(ImageSamplingTest, Load3DOutOfBoundsZReadsZero) {
  float Storage[1][1][1][4] = {{{{1, 2, 3, 4}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage3D(Storage, sizeof(Storage), 1, 1, 1,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load3DFn Fn =
      resolve<Load3DFn>(addWrapper("load3d", "feme.cpu.image.load.3d.v4f32"));
  float Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Z=*/5, /*Mip=*/0, /*Sample=*/0, true,
     Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
}

TEST_F(ImageSamplingTest, Load3DI32FetchesIntegerTexel) {
  int32_t Storage[2][1][1][4] = {}; // [Z][Y][X][Channel].
  Storage[1][0][0][0] = 10;
  Storage[1][0][0][3] = 40;
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage3D(
      Storage, sizeof(Storage), 1, 1, 2, ResourceFormat::R32G32B32A32_SINT,
      Layout);
  FemeImageDescriptor ImageHeap[1] = {Img};
  Load3DI32Fn Fn = resolve<Load3DI32Fn>(
      addWrapper("load3di32", "feme.cpu.image.load.3d.v4i32"));
  int32_t Out[4];
  Fn(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Z=*/1, /*Mip=*/0, true, Out);
  EXPECT_EQ(Out[0], 10);
  EXPECT_EQ(Out[3], 40);
}

TEST_F(ImageSamplingTest, Store3DWritesTexelIntoTheAddressedSliceOnly) {
  float Storage[2][2][2][4] = {}; // [Z][Y][X][Channel].
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage3D(Storage, sizeof(Storage), 2, 2, 2,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout,
                 FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store3DFn Store = resolveRuntime<Store3DFn>("feme.cpu.image.store.3d.v4f32");
  FeMeTestV4F32 Texel = {5.0f, 6.0f, 7.0f, 8.0f};
  Store(ImageHeap, 1, 0, /*X=*/1, /*Y=*/1, /*Z=*/1, Texel, /*Mask=*/true);

  EXPECT_FLOAT_EQ(Storage[1][1][1][0], 5.0f);
  EXPECT_FLOAT_EQ(Storage[1][1][1][3], 8.0f);
  // Slice 0's identical (X, Y) is untouched -- confirms the write actually
  // addresses one depth slice, not every slice.
  EXPECT_FLOAT_EQ(Storage[0][1][1][0], 0.0f);
}

TEST_F(ImageSamplingTest, Store3DI32WritesIntegerTexel) {
  int32_t Storage[1][1][1][4] = {};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img = makeImage3D(
      Storage, sizeof(Storage), 1, 1, 1, ResourceFormat::R32G32B32A32_SINT,
      Layout, FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store3DI32Fn Store =
      resolveRuntime<Store3DI32Fn>("feme.cpu.image.store.3d.v4i32");
  FeMeTestV4I32 Texel = {-7, 0, 0, 0};
  Store(ImageHeap, 1, 0, 0, 0, 0, Texel, /*Mask=*/true);
  EXPECT_EQ(Storage[0][0][0][0], -7);
}

TEST_F(ImageSamplingTest, Store3DOutOfBoundsZIsANoOp) {
  float Storage[1][1][1][4] = {{{{1, 2, 3, 4}}}};
  FemeImageSubresourceLayout Layout;
  FemeImageDescriptor Img =
      makeImage3D(Storage, sizeof(Storage), 1, 1, 1,
                 ResourceFormat::R32G32B32A32_FLOAT, Layout,
                 FEME_IMAGE_STORAGE);
  FemeImageDescriptor ImageHeap[1] = {Img};

  Store3DFn Store = resolveRuntime<Store3DFn>("feme.cpu.image.store.3d.v4f32");
  FeMeTestV4F32 Texel = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(ImageHeap, 1, 0, /*X=*/0, /*Y=*/0, /*Z=*/5, Texel, /*Mask=*/true);
  EXPECT_FLOAT_EQ(Storage[0][0][0][0], 1.0f);
}

} // namespace


