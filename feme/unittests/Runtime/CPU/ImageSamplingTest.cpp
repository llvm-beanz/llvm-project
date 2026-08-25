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
};

using SampleFn = void (*)(const FemeImageDescriptor *, uint32_t,
                          const FemeSamplerDescriptor *, uint32_t, uint32_t,
                          uint32_t, float, float, float, bool, bool, void *);
using SampleCmpFn = void (*)(const FemeImageDescriptor *, uint32_t,
                             const FemeSamplerDescriptor *, uint32_t, uint32_t,
                             uint32_t, float, float, float, bool, float, bool,
                             void *);
using LoadFn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                        int32_t, int32_t, uint32_t, bool, void *);
/// The `feme.cpu.image.load.2d.v4i32` (roadmap E26) counterpart of `LoadFn`,
/// same operand shape but a `<4 x i32>`-shaped `out`.
using LoadI32Fn = void (*)(const FemeImageDescriptor *, uint32_t, uint32_t,
                           int32_t, int32_t, uint32_t, bool, void *);

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
  Samp.MaxLod = 0.0f;
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.75f, 0.25f, 0.0f, true, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, true, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 2.0f);
  EXPECT_FLOAT_EQ(Out[1], 2.0f);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 1.25f, 0.5f, 0.0f, true, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 2.0f, 2.0f, 0.0f, true, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, true, true, Out);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 1.0f, true, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 9.0f);
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
  Fn(ImageHeap, 1, 0, 1, 1, 0, true, Out);
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
  Fn(R32Heap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(RG32Heap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(RGB32Heap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], -2.0f);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
  EXPECT_NEAR(Out[0], 64.0f / 255.0f, 1e-6f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
  EXPECT_FLOAT_EQ(Out[2], 0.0f);
  EXPECT_FLOAT_EQ(Out[3], 1.0f);
}

// Roadmap F8b: a multisampled image (`SampleCount > 1`) packs every
// sample of one texel contiguously; `femeRTFetchTexel2D`'s addressing
// must skip `SampleCount` samples' worth of bytes per texel step along a
// row, not one, or texel (1, 0) below would alias sample 1 of texel
// (0, 0). This always reads sample 0 (no caller yet threads an explicit
// sample index through -- see FeMeRuntimeCPU.c's own comment).
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 1.0f);
  Fn(ImageHeap, 1, 0, 1, 0, 0, true, Out);
  EXPECT_FLOAT_EQ(Out[0], 5.0f);
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
  Fn(ImageHeap, 1, SamplerHeap, 1, 0, 0, 0.5f, 0.5f, 0.0f, true,
     /*Mask=*/false, Out);
  EXPECT_FLOAT_EQ(Out[0], 0.0f);
  EXPECT_FLOAT_EQ(Out[1], 0.0f);
}

// Roadmap E26: `feme.cpu.image.load.2d.v4i32`, the integer counterpart of
// `feme.cpu.image.load.2d.v4f32` above -- each exercises one of the
// mandatory-sampled `_UINT`/`_SINT` formats `femeRTUnpackImageTexelI32`
// decodes.

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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
  EXPECT_EQ(Out[0], 1);
  EXPECT_EQ(Out[1], 2);
  EXPECT_EQ(Out[2], 3);
  EXPECT_EQ(Out[3], 65535); // Zero-extended, not sign-extended.
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, true, Out);
  EXPECT_EQ(Out[0], 1);
  EXPECT_EQ(Out[1], 256);
  EXPECT_EQ(Out[2], 512);
  EXPECT_EQ(Out[3], 3);
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
  Fn(ImageHeap, 1, 0, /*X=*/1, /*Y=*/0, 0, true, Out);
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
  Fn(ImageHeap, 1, 0, 0, 0, 0, /*Mask=*/false, Out);
  EXPECT_EQ(Out[0], 0);
  EXPECT_EQ(Out[1], 0);
  EXPECT_EQ(Out[2], 0);
  EXPECT_EQ(Out[3], 0);
}

} // namespace
