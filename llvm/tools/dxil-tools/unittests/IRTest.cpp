//===- IRTest.cpp - Legacy IR construction and verification
//----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/DIBuilder.h"
#include "dxil/IR/Dominators.h"
#include "dxil/IR/IRBuilder.h"
#include "dxil/IR/Intrinsics.h"
#include "dxil/IR/MDBuilder.h"
#include "dxil/IR/Module.h"
#include "dxil/IR/TypeFinder.h"
#include "dxil/IR/ValueHandle.h"
#include "dxil/IR/Verifier.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "gtest/gtest.h"

using namespace dxil;

namespace {

TEST(IR, ModuleLifecycleAndPrinting) {
  LLVMContext Context;
  Module M("legacy", Context);
  IRBuilder<> Builder(Context);
  auto *FT = FunctionType::get(Builder.getInt32Ty(), false);
  auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "answer", &M);
  auto *BB = BasicBlock::Create(Context, "entry", F);
  Builder.SetInsertPoint(BB);
  Builder.CreateRet(Builder.getInt32(42));

  EXPECT_EQ(4u, M.getDwarfVersion());
  EXPECT_EQ(llvm::PICLevel::NotPIC, M.getPICLevel());
  EXPECT_EQ(F, M.getFunction("answer"));
  EXPECT_EQ(nullptr, M.getFunction("missing"));
  EXPECT_FALSE(verifyFunction(*F));
  EXPECT_FALSE(verifyModule(M));

  std::string Text;
  llvm::raw_string_ostream OS(Text);
  M.print(OS, nullptr);
  EXPECT_NE(std::string::npos, Text.find("define i32 @answer()"));
  EXPECT_NE(std::string::npos, Text.find("ret i32 42"));

  M.addModuleFlag(Module::Warning, "Dwarf Version", 3u);
  EXPECT_EQ(3u, M.getDwarfVersion());
  M.setPICLevel(llvm::PICLevel::SmallPIC);
  EXPECT_EQ(llvm::PICLevel::SmallPIC, M.getPICLevel());
  auto *Named = M.getOrInsertNamedMetadata("test");
  EXPECT_EQ(Named, M.getNamedMetadata("test"));
  M.eraseNamedMetadata(Named);
  EXPECT_EQ(nullptr, M.getNamedMetadata("test"));
}

TEST(IR, ConstantUniquingAndFolding) {
  LLVMContext Context;
  IRBuilder<> B(Context);
  EXPECT_EQ(B.getInt32(42), B.getInt32(42));
  EXPECT_NE(B.getInt32(42), B.getInt64(42));
  EXPECT_EQ(B.getInt32(42), B.CreateAdd(B.getInt32(19), B.getInt32(23)));

  auto *Ty = B.getDoubleTy();
  auto *PositiveZero = ConstantFP::get(Ty, 0.0);
  auto *NegativeZero = ConstantFP::getNegativeZero(Ty);
  EXPECT_NE(PositiveZero, NegativeZero);
  EXPECT_TRUE(NegativeZero->isNegativeZeroValue());
  auto *Remainder = llvm::cast<ConstantFP>(
      B.CreateFRem(ConstantFP::get(Ty, 5.5), ConstantFP::get(Ty, 2.0)));
  EXPECT_TRUE(Remainder->isExactlyValue(llvm::APFloat(1.5)));

  for (Type *FloatTy : {B.getHalfTy(), B.getFloatTy(), B.getDoubleTy(),
                        Type::getX86_FP80Ty(Context), Type::getFP128Ty(Context),
                        Type::getPPC_FP128Ty(Context)}) {
    auto *Ones = Constant::getAllOnesValue(FloatTy);
    EXPECT_EQ(FloatTy, Ones->getType());
    EXPECT_TRUE(Ones->isAllOnesValue());
  }
}

TEST(IR, IntrinsicTypeMangling) {
  LLVMContext C;
  EXPECT_EQ("llvm.log.f16",
            Intrinsic::getName(Intrinsic::log, Type::getHalfTy(C)));
  EXPECT_EQ("llvm.log.f32",
            Intrinsic::getName(Intrinsic::log, Type::getFloatTy(C)));
  EXPECT_EQ("llvm.log.f64",
            Intrinsic::getName(Intrinsic::log, Type::getDoubleTy(C)));
  EXPECT_EQ("llvm.log.f80",
            Intrinsic::getName(Intrinsic::log, Type::getX86_FP80Ty(C)));
  EXPECT_EQ("llvm.log.f128",
            Intrinsic::getName(Intrinsic::log, Type::getFP128Ty(C)));
  EXPECT_EQ("llvm.log.ppcf128",
            Intrinsic::getName(Intrinsic::log, Type::getPPC_FP128Ty(C)));
  EXPECT_EQ("llvm.ctpop.i65",
            Intrinsic::getName(Intrinsic::ctpop, IntegerType::get(C, 65)));
  EXPECT_EQ("llvm.log.v4f32",
            Intrinsic::getName(Intrinsic::log,
                               VectorType::get(Type::getFloatTy(C), 4)));
  Module M("intrinsics", C);
  auto *Log =
      Intrinsic::getDeclaration(&M, Intrinsic::log, Type::getFloatTy(C));
  EXPECT_EQ(Intrinsic::log, Log->getIntrinsicID());
  EXPECT_FALSE(verifyModule(M));
}

TEST(IR, DominanceAndStableBlockNumbers) {
  LLVMContext C;
  Module M("cfg", C);
  IRBuilder<> B(C);
  auto *FT = FunctionType::get(B.getInt32Ty(), {B.getInt1Ty()}, false);
  auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "diamond", &M);
  auto *Entry = BasicBlock::Create(C, "entry", F);
  auto *Left = BasicBlock::Create(C, "left", F);
  auto *Right = BasicBlock::Create(C, "right", F);
  auto *Merge = BasicBlock::Create(C, "merge", F);
  B.SetInsertPoint(Entry);
  B.CreateCondBr(&*F->arg_begin(), Left, Right);
  B.SetInsertPoint(Left);
  B.CreateBr(Merge);
  B.SetInsertPoint(Right);
  B.CreateBr(Merge);
  B.SetInsertPoint(Merge);
  auto *Phi = B.CreatePHI(B.getInt32Ty(), 2);
  Phi->addIncoming(B.getInt32(1), Left);
  Phi->addIncoming(B.getInt32(2), Right);
  B.CreateRet(Phi);

  DominatorTree DT;
  DT.recalculate(*F);
  EXPECT_TRUE(DT.dominates(Entry, Merge));
  EXPECT_FALSE(DT.dominates(Left, Merge));
  EXPECT_EQ(Entry, DT.getNode(Merge)->getIDom()->getBlock());
  EXPECT_TRUE(DT.verify());
  EXPECT_FALSE(verifyModule(M));

  unsigned MergeNumber = Merge->getNumber();
  auto *Dead = BasicBlock::Create(C, "dead", F, Left);
  B.SetInsertPoint(Dead);
  B.CreateRet(B.getInt32(0));
  EXPECT_EQ(MergeNumber, Merge->getNumber());
  EXPECT_NE(Left->getNumber(), Dead->getNumber());
  DT.recalculate(*F);
  EXPECT_FALSE(DT.isReachableFromEntry(Dead));
  EXPECT_TRUE(DT.verify());
  Dead->eraseFromParent();
  EXPECT_EQ(MergeNumber, Merge->getNumber());
}

TEST(IR, VerifierRejectsMissingTerminator) {
  LLVMContext C;
  Module M("invalid", C);
  auto *F = Function::Create(FunctionType::get(Type::getVoidTy(C), false),
                             GlobalValue::ExternalLinkage, "broken", &M);
  BasicBlock::Create(C, "entry", F);
  std::string Message;
  llvm::raw_string_ostream OS(Message);
  EXPECT_TRUE(verifyModule(M, &OS));
  EXPECT_NE(std::string::npos, Message.find("does not have terminator"));
}

TEST(IR, BlockNumbersSurviveReorderingAndTransfer) {
  LLVMContext C;
  Module M("blocks", C);
  auto *FT = FunctionType::get(Type::getVoidTy(C), false);
  auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "first", &M);
  auto *G = Function::Create(FT, GlobalValue::ExternalLinkage, "second", &M);
  auto *A = BasicBlock::Create(C, "a", F);
  auto *B = BasicBlock::Create(C, "b", F);
  auto *D = BasicBlock::Create(C, "d", G);
  unsigned ANumber = A->getNumber();
  unsigned BNumber = B->getNumber();
  F->getBasicBlockList().splice(F->begin(), F->getBasicBlockList(),
                                B->getIterator());
  EXPECT_EQ(ANumber, A->getNumber());
  EXPECT_EQ(BNumber, B->getNumber());
  G->getBasicBlockList().splice(G->end(), F->getBasicBlockList(),
                                B->getIterator());
  EXPECT_EQ(G, B->getParent());
  EXPECT_NE(D->getNumber(), B->getNumber());
  EXPECT_LT(B->getNumber(), G->getMaxBlockNumber());
  B->removeFromParent();
  B->insertInto(F);
  EXPECT_EQ(F, B->getParent());
  EXPECT_NE(ANumber, B->getNumber());
  EXPECT_GT(B->getNumber(), BNumber);
}

TEST(IR, VerifierRejectsNonDominatingDefinition) {
  LLVMContext C;
  Module M("invalid-dominance", C);
  IRBuilder<> B(C);
  auto *FT =
      FunctionType::get(B.getInt32Ty(), {B.getInt1Ty(), B.getInt32Ty()}, false);
  auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "broken", &M);
  auto *Entry = BasicBlock::Create(C, "entry", F);
  auto *Left = BasicBlock::Create(C, "left", F);
  auto *Right = BasicBlock::Create(C, "right", F);
  auto *Merge = BasicBlock::Create(C, "merge", F);
  auto Arg = F->arg_begin();
  Value *Condition = &*Arg++;
  Value *Input = &*Arg;
  B.SetInsertPoint(Entry);
  B.CreateCondBr(Condition, Left, Right);
  B.SetInsertPoint(Left);
  auto *Sum = B.CreateAdd(Input, B.getInt32(1));
  B.CreateBr(Merge);
  B.SetInsertPoint(Right);
  B.CreateBr(Merge);
  B.SetInsertPoint(Merge);
  B.CreateRet(Sum);

  std::string Message;
  llvm::raw_string_ostream OS(Message);
  EXPECT_TRUE(verifyModule(M, &OS));
  EXPECT_NE(std::string::npos,
            Message.find("Instruction does not dominate all uses"));
}

TEST(IR, ValueHandlesSurviveMapGrowthAndReplacement) {
  LLVMContext C;
  Module M("handles", C);
  std::vector<GlobalVariable *> Globals;
  std::vector<WeakTrackingVH> Handles;
  for (unsigned I = 0; I != 1024; ++I) {
    auto *GV = new GlobalVariable(M, Type::getInt32Ty(C), false,
                                  GlobalValue::ExternalLinkage, nullptr);
    Globals.push_back(GV);
    Handles.emplace_back(GV);
  }

  WeakTrackingVH Copy(Handles.front());
  Globals.front()->replaceAllUsesWith(Globals.back());
  EXPECT_EQ(Globals.back(), static_cast<Value *>(Handles.front()));
  EXPECT_EQ(Globals.back(), static_cast<Value *>(Copy));
  Globals.back()->eraseFromParent();
  EXPECT_EQ(nullptr, static_cast<Value *>(Handles.front()));
  EXPECT_EQ(nullptr, static_cast<Value *>(Handles.back()));
  EXPECT_EQ(nullptr, static_cast<Value *>(Copy));
  for (unsigned I = 0; I + 1 != Globals.size(); ++I)
    Globals[I]->eraseFromParent();
  for (const auto &Handle : Handles)
    EXPECT_EQ(nullptr, static_cast<Value *>(Handle));
}

TEST(IR, TypeFinderCanRevisitMetadata) {
  LLVMContext C;
  Module M("types", C);
  auto *Ty = StructType::create(C, "record");
  auto *Value = UndefValue::get(PointerType::getUnqual(Ty));
  auto *MD = MDTuple::get(C, ConstantAsMetadata::get(Value));
  EXPECT_EQ(MD, MDTuple::get(C, ConstantAsMetadata::get(Value)));
  M.getOrInsertNamedMetadata("test")->addOperand(MD);
  TypeFinder Finder;
  Finder.run(M, true);
  ASSERT_EQ(1u, Finder.size());
  EXPECT_EQ(Ty, Finder[0]);
  Finder.clear();
  Finder.run(M, true);
  ASSERT_EQ(1u, Finder.size());
  EXPECT_EQ(Ty, Finder[0]);
}

TEST(IR, DebugMetadataLifecycle) {
  LLVMContext C;
  Module M("debug", C);
  DIBuilder DIB(M);
  auto *CU = DIB.createCompileUnit(llvm::dwarf::DW_LANG_C99, "test.c", "/",
                                   "dxil-tools", false, "", 0);
  auto *File = DIB.createFile("test.c", "/");
  auto *Ty = DIB.createBasicType("int", 32, 32, llvm::dwarf::DW_ATE_signed);
  EXPECT_EQ(File, DIB.createFile("test.c", "/"));
  auto *SubTy =
      DIB.createSubroutineType(File, DIB.getOrCreateTypeArray({nullptr}));
  auto *SP =
      DIB.createFunction(File, "f", "f", File, 1, SubTy, false, false, 1);
  auto *Local =
      DIB.createLocalVariable(DW_TAG_auto_variable, SP, "local", File, 1, Ty);
  auto *Arg = DIB.createLocalVariable(DW_TAG_arg_variable, SP, "arg", File, 1,
                                      Ty, false, 0, 1);
  M.getOrInsertNamedMetadata("variables")->addOperand(Local);
  M.getOrInsertNamedMetadata("variables")->addOperand(Arg);
  DIB.retainType(Ty);
  DIB.finalize();
  EXPECT_EQ(1u, CU->getRetainedTypes().size());
  EXPECT_FALSE(verifyModule(M));
  std::string Text;
  llvm::raw_string_ostream OS(Text);
  M.print(OS, nullptr);
  EXPECT_NE(std::string::npos, Text.find("tag: DW_TAG_auto_variable"));
  EXPECT_NE(std::string::npos, Text.find("tag: DW_TAG_arg_variable"));
}

} // namespace
