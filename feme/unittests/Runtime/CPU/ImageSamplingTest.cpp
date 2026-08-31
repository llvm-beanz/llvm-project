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
    return reinterpret_cast<FnTy>(
        Engine->getFunctionAddress(F->getName().str()));
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
/// explicit-LOD sample), `Lod`/`UseExplicitLod`, an active-lane mask, and
/// the `<4 x float>` result written through the trailing `out` pointer.
using SampleFn = void (*)(const FemeImageDescriptor *, uint32_t,
                          const FemeSamplerDescriptor *, uint32_t, uint32_t,
                          uint32_t, float, float, float, float, float, float,
                          float, bool, bool, void *);
using SampleCmpFn = void (*)(const FemeImageDescriptor *, uint32_t,
                             const FemeSamplerDescriptor *, uint32_t, uint32_t,
                             uint32_t, float, float, float, bool, float, bool,
                             void *);
using LoadFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                        int32_t, int32_t, uint32_t, uint32_t, bool, void *);
/// The `feme.cpu.image.load.2d.v4i32` (roadmap E26) counterpart of `LoadFn`,
/// same operand shape but a `<4 x i32>`-shaped `out` -- also takes a
/// `Sample` operand (roadmap H19g), like `LoadFn`'s own.
using LoadI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                           int32_t, int32_t, uint32_t, uint32_t, bool, void *);
/// The roadmap H7b-a `Texture2DArray` counterpart of `SampleFn`, adding a
/// float `ArrayLayer` coordinate (rounded to nearest, clamped) before
/// `Lod`.
using SampleArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                               const FemeSamplerDescriptor *, uint32_t,
                               uint32_t, uint32_t, float, float, float, float,
                               bool, bool, void *);
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
/// direction-vector coordinate (`DirX`, `DirY`, `DirZ`) instead of `(U, V)`.
using SampleCubeFn = void (*)(const FemeImageDescriptor *, uint32_t,
                              const FemeSamplerDescriptor *, uint32_t,
                              uint32_t, uint32_t, float, float, float, float,
                              bool, bool, void *);
/// The roadmap H7b-a `TextureCubeArray` counterpart of `SampleCubeFn`,
/// adding a float `ArrayLayer` coordinate (selecting a six-layer cube
/// element) before `Lod`.
using SampleCubeArrayFn = void (*)(const FemeImageDescriptor *, uint32_t,
                                   const FemeSamplerDescriptor *, uint32_t,
                                   uint32_t, uint32_t, float, float, float,
                                   float, float, bool, bool, void *);

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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.75f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, true, Out);
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
     /*Lod=*/1.0f, /*UseExplicitLod=*/true, true, Out);
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
     /*Lod=*/-1.0f, /*UseExplicitLod=*/true, true, Out);
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
     /*UseExplicitLod=*/false, true, Out);
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
     /*Lod=*/0.5f, /*UseExplicitLod=*/true, true, Out);
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
     /*Lod=*/0.5f, /*UseExplicitLod=*/true, true, Out);
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
     /*UseExplicitLod=*/false, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.25f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 2.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.1f);
  EXPECT_FLOAT_EQ(Out[1], 0.2f);
  EXPECT_FLOAT_EQ(Out[2], 0.3f);
  EXPECT_FLOAT_EQ(Out[3], 0.4f);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, true, true, Out);
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
     /*Lod=*/0.0f, /*UseExplicitLod=*/false, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
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
     /*UseExplicitLod=*/false, true, Out);
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
     /*UseExplicitLod=*/false, true, Out);
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
     /*Lod=*/0.0f, /*UseExplicitLod=*/true, true, Out);
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
     /*Lod=*/0.0f, /*UseExplicitLod=*/true, true, Out);
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
     /*UseExplicitLod=*/false, true, IsotropicOut);

  FemeSamplerDescriptor AnisotropicHeap[1] = {Anisotropic};
  float AnisotropicOut[4];
  Fn(ImageHeap, 1, AnisotropicHeap, 1, 0, 0, 0.5f, 0.5f, /*DUdX=*/0.0f,
     /*DUdY=*/0.0f, /*DVdX=*/0.0f, /*DVdY=*/0.5f, /*Lod=*/0.0f,
     /*UseExplicitLod=*/false, true, AnisotropicOut);

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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, true, 0.4f, true,
     &PassResult);
  EXPECT_FLOAT_EQ(PassResult, 1.0f);
  // Ref (0.6) <= Texel (0.5): fail.
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, true, 0.6f, true,
     &FailResult);
  EXPECT_FLOAT_EQ(FailResult, 0.0f);
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
     0.0f, true, /*Mask=*/false, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/2.0f, 0.0f,
     true, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, /*ArrayLayer=*/0.6f, 0.0f,
     true, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
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
    Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, C.X, C.Y, C.Z, 0.0f, true, true,
       Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f, 0.0f, 0.0f, /*ArrayLayer=*/0.0f,
     0.0f, true, true, Out0);
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.0f, 0.0f, 0.0f, /*ArrayLayer=*/1.0f,
     0.0f, true, true, Out1);
  EXPECT_FLOAT_EQ(Out0[0], 0.0f);
  EXPECT_FLOAT_EQ(Out1[0], 100.0f);
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


