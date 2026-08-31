//===- RuntimeCPUTest.cpp - Tests for libFeMeRuntimeCPU helper IR --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These tests JIT-compile the actual `libFeMeRuntimeCPU` bitcode (see
// feme/runtime/CPU/FeMeRuntimeCPU.c and "Runtime Support Library" in
// feme/docs/FeMeCPUDesign.md) with MCJIT and call its canonical
// `feme.cpu.resource.*` helpers directly against a real, host-allocated
// heap laid out exactly as `feme::cpu::FemeDescriptor`
// (feme/include/feme/Target/CPU/RuntimeABI.h) describes -- this exercises
// the actual bounds-checking and format-conversion logic, not just that the
// IR parses. A thin per-test wrapper function (returning `void` and writing
// its result through an out-parameter) is added to the JIT'd module for
// each call under test, sidestepping any question of how the host's C ABI
// would return an LLVM-vector-typed value.
//
//===----------------------------------------------------------------------===//

#include "feme/Target/CPU/RuntimeCPU.h"
#include "feme/Target/CPU/RuntimeABI.h"

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

using namespace feme::cpu;
using namespace llvm;

namespace {

/// Looks up \p Name in \p M, the way the target platform's mangler actually
/// spells it. `libFeMeRuntimeCPU`'s helpers are all given their canonical
/// dotted name via a GNU `asm` label (see FeMeRuntimeCPU.c), and on Mach-O
/// targets Clang marks such explicit-asm-label names with a leading
/// `GlobalValue::dropLLVMManglingEscape` byte so the backend emits them
/// verbatim, unprefixed -- so the plain dotted name is not always the
/// `GlobalValue`'s actual name. Try both spellings rather than assume one.
Function *getRuntimeFunction(Module &M, StringRef Name) {
  if (Function *F = M.getFunction(Name))
    return F;
  return M.getFunction(("\1" + Name).str());
}

class RuntimeCPUTest : public testing::Test {
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

  /// Adds `void @<Name>(ptr heap, i32 heap_count, i32 descriptor_index, i64
  /// offset, i1 mask, ptr out)` calling the load `feme.cpu.resource.load.*`
  /// function named \p Callee and storing its result through `out`.
  Function *addLoadWrapper(StringRef Name, StringRef Callee) {
    Function *Target = getRuntimeFunction(*M, Callee);
    assert(Target && "runtime function not found in libFeMeRuntimeCPU bitcode");
    Type *ElemTy = Target->getReturnType();
    LLVMContext &C = M->getContext();
    Type *PtrTy = PointerType::get(C, 0);
    FunctionType *WrapperTy =
        FunctionType::get(Type::getVoidTy(C),
                          {PtrTy, Type::getInt32Ty(C), Type::getInt32Ty(C),
                           Type::getInt64Ty(C), Type::getInt1Ty(C), PtrTy},
                          false);
    Function *Wrapper =
        Function::Create(WrapperTy, Function::ExternalLinkage, Name, M);
    BasicBlock *BB = BasicBlock::Create(C, "entry", Wrapper);
    IRBuilder<> Builder(BB);
    auto ArgIt = Wrapper->arg_begin();
    Value *Heap = &*ArgIt++;
    Value *HeapCount = &*ArgIt++;
    Value *Index = &*ArgIt++;
    Value *Offset = &*ArgIt++;
    Value *Mask = &*ArgIt++;
    Value *Out = &*ArgIt++;
    Value *Result =
        Builder.CreateCall(Target, {Heap, HeapCount, Index, Offset, Mask});
    Builder.CreateStore(Result, Out);
    Builder.CreateRetVoid();
    (void)ElemTy;
    return Wrapper;
  }

  /// The store counterpart of `addLoadWrapper`: `void @<Name>(ptr heap, i32
  /// heap_count, i32 descriptor_index, i64 offset, ptr value_ptr, i1 mask)`,
  /// loading the value to store from `value_ptr` (again sidestepping the
  /// vector-argument ABI question).
  ///
  /// A `ValueTy` that Clang's own C ABI lowering would pass by value
  /// differently from its plain IR type -- e.g. `<3 x float>`/`<3 x i32>`,
  /// which get coerced to a `<4 x i32>` register pair (roadmap
  /// H6g-b-a-i-a-i-c: an odd vector width isn't a "natural" by-value
  /// argument shape) -- needs its value adapted to `Target`'s *actual*
  /// (coerced) parameter type before the call, exactly as a real coerced C
  /// call site would: widen with an extra poison lane, then bitcast to the
  /// coerced type. This matches how `feme::cpu::ResourceCalls`' own
  /// generated calls survive being linked against this same coercion (see
  /// "Descriptor formats" in FeMeCPUDesign.md): the call is only safe
  /// because `feme::cpu::ResourceLoweringPass` builds it with the logical,
  /// uncoerced type in the *caller* module before `Linker::linkInModule`
  /// merges in this runtime bitcode -- not because the coercion doesn't
  /// exist.
  Function *addStoreWrapper(StringRef Name, StringRef Callee, Type *ValueTy) {
    Function *Target = getRuntimeFunction(*M, Callee);
    assert(Target && "runtime function not found in libFeMeRuntimeCPU bitcode");
    LLVMContext &C = M->getContext();
    Type *PtrTy = PointerType::get(C, 0);
    FunctionType *WrapperTy =
        FunctionType::get(Type::getVoidTy(C),
                          {PtrTy, Type::getInt32Ty(C), Type::getInt32Ty(C),
                           Type::getInt64Ty(C), PtrTy, Type::getInt1Ty(C)},
                          false);
    Function *Wrapper =
        Function::Create(WrapperTy, Function::ExternalLinkage, Name, M);
    BasicBlock *BB = BasicBlock::Create(C, "entry", Wrapper);
    IRBuilder<> Builder(BB);
    auto ArgIt = Wrapper->arg_begin();
    Value *Heap = &*ArgIt++;
    Value *HeapCount = &*ArgIt++;
    Value *Index = &*ArgIt++;
    Value *Offset = &*ArgIt++;
    Value *ValuePtr = &*ArgIt++;
    Value *Mask = &*ArgIt++;
    Value *Value_ = Builder.CreateLoad(ValueTy, ValuePtr);
    Type *ActualParamTy = Target->getFunctionType()->getParamType(4);
    if (ActualParamTy != ValueTy) {
      auto *NarrowVecTy = cast<FixedVectorType>(ValueTy);
      unsigned NarrowCount = NarrowVecTy->getNumElements();
      SmallVector<int, 4> WidenMask;
      for (unsigned I = 0; I < NarrowCount; ++I)
        WidenMask.push_back(I);
      WidenMask.push_back(-1); // One extra poison lane, matching Clang.
      Value *Widened = Builder.CreateShuffleVector(
          Value_, PoisonValue::get(ValueTy), WidenMask);
      Value_ = Builder.CreateBitCast(Widened, ActualParamTy);
    }
    Builder.CreateCall(Target, {Heap, HeapCount, Index, Offset, Value_, Mask});
    Builder.CreateRetVoid();
    return Wrapper;
  }

  using LoadFn = void (*)(void *, uint32_t, uint32_t, uint64_t, bool, void *);
  using StoreFn = void (*)(void *, uint32_t, uint32_t, uint64_t, void *, bool);

  /// MCJIT compiles the whole module the first time any function's address
  /// is resolved, so a test needing more than one wrapper (see
  /// `RawLoadStoreRoundTrip` below) must add all of them with
  /// `addLoadWrapper`/`addStoreWrapper` before resolving any of their
  /// addresses with `resolve`. `getLoadWrapper`/`getStoreWrapper` are the
  /// add-then-immediately-resolve shorthand for the common single-wrapper
  /// case.
  template <typename FnTy> FnTy resolve(Function *F) {
    uint64_t Addr = Engine->getFunctionAddress(F->getName().str());
    // A null address means MCJIT failed to resolve/finalize this symbol;
    // calling through it crashes with an uninformative `pc=0x0` segfault
    // and no diagnostic at all (see ImageSamplingTest.cpp's identical
    // helper for the specific reported failure this guards against).
    assert(Addr && "MCJIT failed to resolve a runtime function address");
    return reinterpret_cast<FnTy>(Addr);
  }

  LoadFn getLoadWrapper(StringRef Name, StringRef Callee) {
    return resolve<LoadFn>(addLoadWrapper(Name, Callee));
  }

  StoreFn getStoreWrapper(StringRef Name, StringRef Callee, Type *ValueTy) {
    return resolve<StoreFn>(addStoreWrapper(Name, Callee, ValueTy));
  }
};

TEST_F(RuntimeCPUTest, TypedLoadIdentityFormat) {
  float Storage[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_identity",
                               "feme.cpu.resource.load.typed.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 1.0f);
  EXPECT_FLOAT_EQ(Result[1], 2.0f);
  EXPECT_FLOAT_EQ(Result[2], 3.0f);
  EXPECT_FLOAT_EQ(Result[3], 4.0f);
}

TEST_F(RuntimeCPUTest, TypedLoadPackedR8G8B8A8Unorm) {
  // R=255, G=128, B=64, A=0, little-endian.
  uint32_t Storage = 0x00u << 24 | 0x40u << 16 | 0x80u << 8 | 0xFFu;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R8G8B8A8_UNORM);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_packed",
                               "feme.cpu.resource.load.typed.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_NEAR(Result[0], 1.0f, 1e-6f);
  EXPECT_NEAR(Result[1], 128.0f / 255.0f, 1e-6f);
  EXPECT_NEAR(Result[2], 64.0f / 255.0f, 1e-6f);
  EXPECT_NEAR(Result[3], 0.0f, 1e-6f);
}

TEST_F(RuntimeCPUTest, TypedLoadPackedR8G8B8A8Snorm) {
  // R=127 (1.0), G=-127 (-1.0), B=64, A=-128 (clamped to -1.0), little-endian.
  uint32_t Storage = 0x80u << 24 | 0x40u << 16 | 0x81u << 8 | 0x7Fu;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R8G8B8A8_SNORM);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_snorm_packed",
                               "feme.cpu.resource.load.typed.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_NEAR(Result[0], 1.0f, 1e-6f);
  EXPECT_NEAR(Result[1], -1.0f, 1e-6f);
  EXPECT_NEAR(Result[2], 64.0f / 127.0f, 1e-6f);
  EXPECT_NEAR(Result[3], -1.0f, 1e-6f); // -128 clamps to -1.0.
}

TEST_F(RuntimeCPUTest, TypedLoadOutOfBoundsIndexReadsZeroWithoutTouchingHeap) {
  LoadFn Load = getLoadWrapper("test_typed_load_oob",
                               "feme.cpu.resource.load.typed.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {9.0f, 9.0f, 9.0f, 9.0f};
  // heap_count == 0: the descriptor index is always out of range, and
  // %heap itself is never dereferenced (see "Bounds checking").
  Load(nullptr, 0, 0, 0, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 0.0f);
  EXPECT_FLOAT_EQ(Result[1], 0.0f);
  EXPECT_FLOAT_EQ(Result[2], 0.0f);
  EXPECT_FLOAT_EQ(Result[3], 0.0f);
}

TEST_F(RuntimeCPUTest, TypedLoadInactiveMaskReadsZero) {
  float Storage[4] = {5.0f, 5.0f, 5.0f, 5.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_inactive_mask",
                               "feme.cpu.resource.load.typed.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  Load(Heap, 1, 0, 0, /*mask=*/false, Result);
  EXPECT_FLOAT_EQ(Result[0], 0.0f);
}

TEST_F(RuntimeCPUTest, TypedLoadKindMismatchIsTreatedAsOutOfBounds) {
  // A `Kind::CBuffer` descriptor read through the typed-buffer helper: "a
  // descriptor kind mismatch ... is treated as an out-of-bounds access"
  // (see "Lowering").
  float Storage[4] = {2.0f, 2.0f, 2.0f, 2.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::CBuffer);

  LoadFn Load = getLoadWrapper("test_typed_load_kind_mismatch",
                               "feme.cpu.resource.load.typed.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 0.0f);
}

TEST_F(RuntimeCPUTest, TypedStoreRoundTrips) {
  float Storage[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  StoreFn Store =
      getStoreWrapper("test_typed_store", "feme.cpu.resource.store.typed.v4f32",
                      FixedVectorType::get(Type::getFloatTy(Ctx), 4));
  ASSERT_TRUE(Store);
  float ToStore[4] = {7.0f, 8.0f, 9.0f, 10.0f};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_FLOAT_EQ(Storage[0], 7.0f);
  EXPECT_FLOAT_EQ(Storage[1], 8.0f);
  EXPECT_FLOAT_EQ(Storage[2], 9.0f);
  EXPECT_FLOAT_EQ(Storage[3], 10.0f);
}

TEST_F(RuntimeCPUTest, TypedStoreDroppedWithoutUavFlag) {
  // An SRV (no `FEME_DESCRIPTOR_UAV` bit) is read-only: the store must be
  // silently dropped rather than corrupting it.
  float Storage[4] = {3.0f, 3.0f, 3.0f, 3.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  StoreFn Store = getStoreWrapper(
      "test_typed_store_no_uav", "feme.cpu.resource.store.typed.v4f32",
      FixedVectorType::get(Type::getFloatTy(Ctx), 4));
  ASSERT_TRUE(Store);
  float ToStore[4] = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_FLOAT_EQ(Storage[0], 3.0f);
}

TEST_F(RuntimeCPUTest, TypedStoreSnormRoundTrips) {
  uint32_t Storage = 0;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R8G8B8A8_SNORM);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  // Both wrappers must be added before either address is resolved (see
  // `resolve`'s comment above): MCJIT compiles the whole module on the
  // first address resolution.
  Function *StoreWrapper = addStoreWrapper(
      "test_typed_store_snorm", "feme.cpu.resource.store.typed.v4f32",
      FixedVectorType::get(Type::getFloatTy(Ctx), 4));
  Function *LoadWrapper = addLoadWrapper("test_typed_load_snorm_roundtrip",
                                         "feme.cpu.resource.load.typed.v4f32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  // 1.0, -1.0, 0.5 (rounds to 64/127), out-of-range 2.0 clamped to 1.0.
  float ToStore[4] = {1.0f, -1.0f, 0.5f, 2.0f};
  Store(Heap, 1, 0, 0, ToStore, true);

  float Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_NEAR(Result[0], 1.0f, 1e-6f);
  EXPECT_NEAR(Result[1], -1.0f, 1e-6f);
  EXPECT_NEAR(Result[2], 64.0f / 127.0f, 1e-2f);
  EXPECT_NEAR(Result[3], 1.0f, 1e-6f);
}

TEST_F(RuntimeCPUTest, TypedLoadV4I32IdentityFormat) {
  // (V4) `<4 x i32>`, the `R32G32B32A32_UINT`/`_SINT` identity-format
  // counterpart of the `<4 x float>` view above -- see
  // femeCpuResourceLoadTypedV4I32 in FeMeRuntimeCPU.c.
  int32_t Storage[4] = {-1, 2, -3, 4};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_SINT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_v4i32",
                               "feme.cpu.resource.load.typed.v4i32");
  ASSERT_TRUE(Load);
  int32_t Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_EQ(Result[0], -1);
  EXPECT_EQ(Result[1], 2);
  EXPECT_EQ(Result[2], -3);
  EXPECT_EQ(Result[3], 4);
}

TEST_F(RuntimeCPUTest, TypedLoadV4I32InactiveMaskReadsZero) {
  int32_t Storage[4] = {5, 5, 5, 5};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_UINT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_v4i32_inactive_mask",
                               "feme.cpu.resource.load.typed.v4i32");
  ASSERT_TRUE(Load);
  int32_t Result[4] = {1, 1, 1, 1};
  Load(Heap, 1, 0, 0, /*mask=*/false, Result);
  EXPECT_EQ(Result[0], 0);
}

TEST_F(RuntimeCPUTest, TypedStoreV4I32RoundTrips) {
  int32_t Storage[4] = {0, 0, 0, 0};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_UINT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  StoreFn Store = getStoreWrapper(
      "test_typed_store_v4i32", "feme.cpu.resource.store.typed.v4i32",
      FixedVectorType::get(Type::getInt32Ty(Ctx), 4));
  ASSERT_TRUE(Store);
  int32_t ToStore[4] = {7, 8, 9, 10};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_EQ(Storage[0], 7);
  EXPECT_EQ(Storage[1], 8);
  EXPECT_EQ(Storage[2], 9);
  EXPECT_EQ(Storage[3], 10);
}

TEST_F(RuntimeCPUTest, TypedStoreV4I32DroppedWithoutUavFlag) {
  int32_t Storage[4] = {3, 3, 3, 3};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_UINT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  StoreFn Store = getStoreWrapper(
      "test_typed_store_v4i32_no_uav", "feme.cpu.resource.store.typed.v4i32",
      FixedVectorType::get(Type::getInt32Ty(Ctx), 4));
  ASSERT_TRUE(Store);
  int32_t ToStore[4] = {9, 9, 9, 9};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_EQ(Storage[0], 3);
}

TEST_F(RuntimeCPUTest, TypedLoadPackedR8G8B8A8Uint) {
  // R=255, G=128, B=64, A=0, little-endian, zero-extended to i32.
  uint32_t Storage = 0x00u << 24 | 0x40u << 16 | 0x80u << 8 | 0xFFu;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R8G8B8A8_UINT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_uint_packed",
                               "feme.cpu.resource.load.typed.v4i32");
  ASSERT_TRUE(Load);
  int32_t Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_EQ(Result[0], 255);
  EXPECT_EQ(Result[1], 128);
  EXPECT_EQ(Result[2], 64);
  EXPECT_EQ(Result[3], 0);
}

TEST_F(RuntimeCPUTest, TypedLoadPackedR8G8B8A8Sint) {
  // R=-1 (0xFF), G=127 (0x7F), B=-128 (0x80), A=1, little-endian,
  // sign-extended to i32.
  uint32_t Storage = 0x01u << 24 | 0x80u << 16 | 0x7Fu << 8 | 0xFFu;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R8G8B8A8_SINT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_sint_packed",
                               "feme.cpu.resource.load.typed.v4i32");
  ASSERT_TRUE(Load);
  int32_t Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_EQ(Result[0], -1);
  EXPECT_EQ(Result[1], 127);
  EXPECT_EQ(Result[2], -128);
  EXPECT_EQ(Result[3], 1);
}

TEST_F(RuntimeCPUTest, TypedStorePackedR8G8B8A8UintRoundTrips) {
  uint32_t Storage = 0;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R8G8B8A8_UINT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  // Both wrappers must be added before either address is resolved (see
  // `resolve`'s comment above): MCJIT compiles the whole module on the
  // first address resolution.
  Function *StoreWrapper = addStoreWrapper(
      "test_typed_store_uint_packed", "feme.cpu.resource.store.typed.v4i32",
      FixedVectorType::get(Type::getInt32Ty(Ctx), 4));
  Function *LoadWrapper =
      addLoadWrapper("test_typed_load_uint_packed_roundtrip",
                     "feme.cpu.resource.load.typed.v4i32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  int32_t ToStore[4] = {255, 128, 64, 0};
  Store(Heap, 1, 0, 0, ToStore, true);

  int32_t Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_EQ(Result[0], 255);
  EXPECT_EQ(Result[1], 128);
  EXPECT_EQ(Result[2], 64);
  EXPECT_EQ(Result[3], 0);
}

TEST_F(RuntimeCPUTest, TypedStorePackedR8G8B8A8SintRoundTrips) {
  uint32_t Storage = 0;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R8G8B8A8_SINT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  Function *StoreWrapper = addStoreWrapper(
      "test_typed_store_sint_packed", "feme.cpu.resource.store.typed.v4i32",
      FixedVectorType::get(Type::getInt32Ty(Ctx), 4));
  Function *LoadWrapper =
      addLoadWrapper("test_typed_load_sint_packed_roundtrip",
                     "feme.cpu.resource.load.typed.v4i32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  int32_t ToStore[4] = {-1, 127, -128, 1};
  Store(Heap, 1, 0, 0, ToStore, true);

  int32_t Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_EQ(Result[0], -1);
  EXPECT_EQ(Result[1], 127);
  EXPECT_EQ(Result[2], -128);
  EXPECT_EQ(Result[3], 1);
}

TEST_F(RuntimeCPUTest, TypedLoadOutOfRangeOffsetReadsZero) {
  float Storage[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage); // Room for one element only.
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);

  LoadFn Load = getLoadWrapper("test_typed_load_offset_oob",
                               "feme.cpu.resource.load.typed.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  // Element index 1 is past the one-element buffer.
  Load(Heap, 1, 0, 1, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 0.0f);
}

TEST_F(RuntimeCPUTest, TrustedFlagSkipsOffsetCheck) {
  // FEME_DESCRIPTOR_TRUSTED lets a deliberately over-reported access
  // through -- see "Per-descriptor control". The buffer is intentionally
  // undersized here to prove the check really was skipped.
  float Storage[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = 4; // Only one float's worth, not four.
  Heap[0].Format = static_cast<uint32_t>(ResourceFormat::R32G32B32A32_FLOAT);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Typed);
  Heap[0].Flags = FEME_DESCRIPTOR_TRUSTED;

  LoadFn Load = getLoadWrapper("test_typed_load_trusted",
                               "feme.cpu.resource.load.typed.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 1.0f);
  EXPECT_FLOAT_EQ(Result[1], 2.0f);
}

TEST_F(RuntimeCPUTest, RawLoadStoreRoundTrip) {
  int32_t Storage = 0;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  Function *StoreWrapper =
      addStoreWrapper("test_raw_store", "feme.cpu.resource.store.raw.i32",
                      Type::getInt32Ty(Ctx));
  Function *LoadWrapper =
      addLoadWrapper("test_raw_load", "feme.cpu.resource.load.raw.i32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  int32_t ToStore = 42;
  Store(Heap, 1, 0, 0, &ToStore, true);
  EXPECT_EQ(Storage, 42);

  int32_t Result = 0;
  Load(Heap, 1, 0, 0, true, &Result);
  EXPECT_EQ(Result, 42);
}

TEST_F(RuntimeCPUTest, StructuredBufferKindIsAccepted) {
  // The raw-family calls accept both `Kind::Raw` (ByteAddressBuffer) and
  // `Kind::Structured` (StructuredBuffer) -- see "Descriptor heaps".
  float Storage = 3.5f;
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = &Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Structured);

  LoadFn Load =
      getLoadWrapper("test_structured_load", "feme.cpu.resource.load.raw.f32");
  ASSERT_TRUE(Load);
  float Result = 0.0f;
  Load(Heap, 1, 0, 0, true, &Result);
  EXPECT_FLOAT_EQ(Result, 3.5f);
}

// Regression tests for roadmap H3a: ResourceCalls.cpp's mangleResourceCallName
// generically supports vector-typed raw/structured buffer element loads (e.g.
// a whole-`vec4` load out of a UBO/SSBO, as `out_color = color[idx]` lowers
// to), but until this milestone the CPU runtime only defined the scalar
// `feme.cpu.resource.load.raw.i32`/`.f32` helpers -- so any shader lowering
// to a vector raw load hit a late JIT "Symbols not found" failure. Verify the
// new `feme.cpu.resource.load.raw.v4f32`/`.store.raw.v4f32` helpers round-trip
// a whole `<4 x float>` correctly.
TEST_F(RuntimeCPUTest, RawLoadV4F32IdentityFormat) {
  float Storage[4] = {1.0f, -2.0f, 3.5f, -4.5f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);

  LoadFn Load = getLoadWrapper("test_raw_load_v4f32",
                               "feme.cpu.resource.load.raw.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 1.0f);
  EXPECT_FLOAT_EQ(Result[1], -2.0f);
  EXPECT_FLOAT_EQ(Result[2], 3.5f);
  EXPECT_FLOAT_EQ(Result[3], -4.5f);
}

TEST_F(RuntimeCPUTest, RawLoadV4F32InactiveMaskReadsZero) {
  float Storage[4] = {9.0f, 9.0f, 9.0f, 9.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);

  LoadFn Load = getLoadWrapper("test_raw_load_v4f32_inactive_mask",
                               "feme.cpu.resource.load.raw.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  Load(Heap, 1, 0, 0, /*mask=*/false, Result);
  EXPECT_FLOAT_EQ(Result[0], 0.0f);
}

TEST_F(RuntimeCPUTest, RawStoreV4F32RoundTrips) {
  float Storage[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  StoreFn Store = getStoreWrapper(
      "test_raw_store_v4f32", "feme.cpu.resource.store.raw.v4f32",
      FixedVectorType::get(Type::getFloatTy(Ctx), 4));
  ASSERT_TRUE(Store);
  float ToStore[4] = {2.0f, 4.0f, 6.0f, 8.0f};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_FLOAT_EQ(Storage[0], 2.0f);
  EXPECT_FLOAT_EQ(Storage[1], 4.0f);
  EXPECT_FLOAT_EQ(Storage[2], 6.0f);
  EXPECT_FLOAT_EQ(Storage[3], 8.0f);
}

TEST_F(RuntimeCPUTest, RawStoreV4F32DroppedWithoutUavFlag) {
  float Storage[4] = {3.0f, 3.0f, 3.0f, 3.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);

  StoreFn Store = getStoreWrapper(
      "test_raw_store_v4f32_no_uav", "feme.cpu.resource.store.raw.v4f32",
      FixedVectorType::get(Type::getFloatTy(Ctx), 4));
  ASSERT_TRUE(Store);
  float ToStore[4] = {9.0f, 9.0f, 9.0f, 9.0f};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_FLOAT_EQ(Storage[0], 3.0f);
}

TEST_F(RuntimeCPUTest, RawLoadV4F32StructuredKindIsAccepted) {
  float Storage[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Structured);

  LoadFn Load = getLoadWrapper("test_raw_load_v4f32_structured",
                               "feme.cpu.resource.load.raw.v4f32");
  ASSERT_TRUE(Load);
  float Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 1.0f);
  EXPECT_FLOAT_EQ(Result[3], 4.0f);
}

// Regression tests for roadmap H6g-b-a-i-a-i-c: a GLSL `vec2`/`vec3`/
// `ivec2`/`ivec3` mesh-shader input/output (e.g. a whole-`vec2` load out of
// a `uniform Foo { vec2 v[N]; }` block) needs a raw-buffer-load/store
// overload narrower than `V4F32`'s whole `<4 x float>`, or an integer one
// at all -- until this milestone the runtime only defined the scalar and
// full-`<4 x float>`-width overloads, so any shader lowering to one of
// these calls hit a late JIT "Symbols not found" failure at
// `vkCreateGraphicsPipelines` time. Verify each new
// `feme.cpu.resource.{load,store}.raw.{v2f32,v3f32,v2i32,v3i32,v4i32}`
// helper round-trips its value correctly.
TEST_F(RuntimeCPUTest, RawLoadStoreRoundTripV2F32) {
  float Storage[2] = {0.0f, 0.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  Function *StoreWrapper = addStoreWrapper(
      "test_raw_store_v2f32", "feme.cpu.resource.store.raw.v2f32",
      FixedVectorType::get(Type::getFloatTy(Ctx), 2));
  Function *LoadWrapper = addLoadWrapper("test_raw_load_v2f32",
                                        "feme.cpu.resource.load.raw.v2f32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  float ToStore[2] = {1.5f, -2.5f};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_FLOAT_EQ(Storage[0], 1.5f);
  EXPECT_FLOAT_EQ(Storage[1], -2.5f);

  float Result[2] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 1.5f);
  EXPECT_FLOAT_EQ(Result[1], -2.5f);
}

TEST_F(RuntimeCPUTest, RawLoadStoreRoundTripV3F32) {
  float Storage[3] = {0.0f, 0.0f, 0.0f};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  Function *StoreWrapper = addStoreWrapper(
      "test_raw_store_v3f32", "feme.cpu.resource.store.raw.v3f32",
      FixedVectorType::get(Type::getFloatTy(Ctx), 3));
  Function *LoadWrapper = addLoadWrapper("test_raw_load_v3f32",
                                        "feme.cpu.resource.load.raw.v3f32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  float ToStore[3] = {11.0f, 22.0f, 33.0f};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_FLOAT_EQ(Storage[0], 11.0f);
  EXPECT_FLOAT_EQ(Storage[1], 22.0f);
  EXPECT_FLOAT_EQ(Storage[2], 33.0f);

  float Result[3] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_FLOAT_EQ(Result[0], 11.0f);
  EXPECT_FLOAT_EQ(Result[1], 22.0f);
  EXPECT_FLOAT_EQ(Result[2], 33.0f);
}

TEST_F(RuntimeCPUTest, RawLoadStoreRoundTripV2I32) {
  int32_t Storage[2] = {0, 0};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  Function *StoreWrapper = addStoreWrapper(
      "test_raw_store_v2i32", "feme.cpu.resource.store.raw.v2i32",
      FixedVectorType::get(Type::getInt32Ty(Ctx), 2));
  Function *LoadWrapper = addLoadWrapper("test_raw_load_v2i32",
                                        "feme.cpu.resource.load.raw.v2i32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  int32_t ToStore[2] = {7, -13};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_EQ(Storage[0], 7);
  EXPECT_EQ(Storage[1], -13);

  int32_t Result[2] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_EQ(Result[0], 7);
  EXPECT_EQ(Result[1], -13);
}

TEST_F(RuntimeCPUTest, RawLoadStoreRoundTripV3I32) {
  int32_t Storage[3] = {0, 0, 0};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  Function *StoreWrapper = addStoreWrapper(
      "test_raw_store_v3i32", "feme.cpu.resource.store.raw.v3i32",
      FixedVectorType::get(Type::getInt32Ty(Ctx), 3));
  Function *LoadWrapper = addLoadWrapper("test_raw_load_v3i32",
                                        "feme.cpu.resource.load.raw.v3i32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  int32_t ToStore[3] = {1, -2, 3};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_EQ(Storage[0], 1);
  EXPECT_EQ(Storage[1], -2);
  EXPECT_EQ(Storage[2], 3);

  int32_t Result[3] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_EQ(Result[0], 1);
  EXPECT_EQ(Result[1], -2);
  EXPECT_EQ(Result[2], 3);
}

TEST_F(RuntimeCPUTest, RawLoadStoreRoundTripV4I32) {
  int32_t Storage[4] = {0, 0, 0, 0};
  FemeDescriptor Heap[1] = {};
  Heap[0].Data = Storage;
  Heap[0].SizeInBytes = sizeof(Storage);
  Heap[0].Kind = static_cast<uint32_t>(ResourceKind::Raw);
  Heap[0].Flags = FEME_DESCRIPTOR_UAV;

  Function *StoreWrapper = addStoreWrapper(
      "test_raw_store_v4i32", "feme.cpu.resource.store.raw.v4i32",
      FixedVectorType::get(Type::getInt32Ty(Ctx), 4));
  Function *LoadWrapper = addLoadWrapper("test_raw_load_v4i32",
                                        "feme.cpu.resource.load.raw.v4i32");
  StoreFn Store = resolve<StoreFn>(StoreWrapper);
  LoadFn Load = resolve<LoadFn>(LoadWrapper);
  ASSERT_TRUE(Store);
  ASSERT_TRUE(Load);

  int32_t ToStore[4] = {4, -3, 2, -1};
  Store(Heap, 1, 0, 0, ToStore, true);
  EXPECT_EQ(Storage[0], 4);
  EXPECT_EQ(Storage[3], -1);

  int32_t Result[4] = {};
  Load(Heap, 1, 0, 0, true, Result);
  EXPECT_EQ(Result[0], 4);
  EXPECT_EQ(Result[3], -1);
}

// Inactive-mask/no-UAV/out-of-bounds behaviour is already covered by the
// `TypedLoad*`/`RawLoad(Store)?V4F32*` tests above -- `femeRTLoadDescriptor`
// and `femeRTCheckAccess` are shared by every `Raw`/`Structured` overload
// regardless of element width, so those checks aren't re-tested per width;
// this row only needs to confirm each new width's own descriptor-offset
// size (`sizeof(FemeRTv2f32)`/etc.) and its call name are wired up
// correctly, which the round-trip tests above already do.

} // namespace
