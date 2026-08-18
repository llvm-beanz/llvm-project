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
    return reinterpret_cast<FnTy>(
        Engine->getFunctionAddress(F->getName().str()));
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

} // namespace
