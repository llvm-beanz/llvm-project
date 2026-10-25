//===- IRTest.cpp - Legacy IR construction and verification
//----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/AutoUpgrade.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/DebugInfoMetadata.h"
#include "dxil/IR/Dominators.h"
#include "dxil/IR/Instructions.h"
#include "dxil/IR/Intrinsics.h"
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
  auto *FT = FunctionType::get(Type::getInt32Ty(Context), false);
  auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "answer", &M);
  auto *BB = BasicBlock::Create(Context, "entry", F);
  ReturnInst::Create(Context, ConstantInt::get(Type::getInt32Ty(Context), 42),
                     BB);

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
  auto *Int32Ty = Type::getInt32Ty(Context);
  EXPECT_EQ(ConstantInt::get(Int32Ty, 42), ConstantInt::get(Int32Ty, 42));
  EXPECT_NE(ConstantInt::get(Int32Ty, 42),
            ConstantInt::get(Type::getInt64Ty(Context), 42));
  EXPECT_EQ(ConstantInt::get(Int32Ty, 42),
            ConstantExpr::getAdd(ConstantInt::get(Int32Ty, 19),
                                 ConstantInt::get(Int32Ty, 23)));

  auto *Ty = Type::getDoubleTy(Context);
  auto *PositiveZero = ConstantFP::get(Ty, 0.0);
  auto *NegativeZero = ConstantFP::getNegativeZero(Ty);
  EXPECT_NE(PositiveZero, NegativeZero);
  EXPECT_TRUE(NegativeZero->isNegativeZeroValue());
  auto *Remainder = llvm::cast<ConstantFP>(ConstantExpr::getFRem(
      ConstantFP::get(Ty, 5.5), ConstantFP::get(Ty, 2.0)));
  EXPECT_TRUE(Remainder->isExactlyValue(llvm::APFloat(1.5)));

  for (Type *FloatTy :
       {Type::getHalfTy(Context), Type::getFloatTy(Context),
        Type::getDoubleTy(Context), Type::getX86_FP80Ty(Context),
        Type::getFP128Ty(Context), Type::getPPC_FP128Ty(Context)}) {
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

TEST(IR, FunctionEntryCountMetadata) {
  LLVMContext C;
  Module M("profile", C);
  auto *F = Function::Create(FunctionType::get(Type::getVoidTy(C), false),
                             GlobalValue::ExternalLinkage, "f", &M);
  ReturnInst::Create(C, BasicBlock::Create(C, "entry", F));
  EXPECT_FALSE(F->getEntryCount().has_value());
  for (uint64_t Count : {uint64_t(0), uint64_t(42), UINT64_MAX}) {
    F->setEntryCount(Count);
    EXPECT_EQ(Count, F->getEntryCount());
    auto *MD = F->getMetadata(LLVMContext::MD_prof);
    ASSERT_NE(nullptr, MD);
    ASSERT_EQ(2u, MD->getNumOperands());
    EXPECT_EQ("function_entry_count",
              llvm::cast<MDString>(MD->getOperand(0))->getString());
    auto *Value = mdconst::extract<ConstantInt>(MD->getOperand(1));
    EXPECT_TRUE(Value->getType()->isIntegerTy(64));
    EXPECT_EQ(Count, Value->getZExtValue());
  }
  EXPECT_FALSE(verifyModule(M));
}

TEST(IR, IntrinsicCallUpgrades) {
  LLVMContext C;
  auto *IntTy = Type::getInt32Ty(C);
  auto *VecTy = VectorType::get(IntTy, 4);
  struct Upgrade {
    const char *Name;
    Type *Ty;
    Intrinsic::ID ID;
  };
  for (const auto &U :
       {Upgrade{"llvm.ctlz.i32", IntTy, Intrinsic::ctlz},
        Upgrade{"llvm.cttz.i32", IntTy, Intrinsic::cttz},
        Upgrade{"llvm.arm.neon.vclz.v4i32", VecTy, Intrinsic::ctlz},
        Upgrade{"llvm.arm.neon.vcnt.v4i32", VecTy, Intrinsic::ctpop}}) {
    SCOPED_TRACE(U.Name);
    Module M("upgrade", C);
    auto *FT = FunctionType::get(U.Ty, {U.Ty}, false);
    auto *Old = Function::Create(FT, GlobalValue::ExternalLinkage, U.Name, &M);
    auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "f", &M);
    auto *BB = BasicBlock::Create(C, "entry", F);
    auto *Call = CallInst::Create(Old, {&*F->arg_begin()}, "count", BB);
    auto *Ret = ReturnInst::Create(C, Call, BB);
    UpgradeCallsToIntrinsic(Old);
    auto *NewCall = llvm::cast<CallInst>(&BB->front());
    EXPECT_EQ(U.ID, NewCall->getCalledFunction()->getIntrinsicID());
    EXPECT_EQ(Intrinsic::getName(U.ID, U.Ty),
              NewCall->getCalledFunction()->getName());
    EXPECT_EQ(&*F->arg_begin(), NewCall->getArgOperand(0));
    EXPECT_EQ(NewCall, Ret->getReturnValue());
    if (U.ID == Intrinsic::ctpop) {
      EXPECT_EQ(1u, NewCall->getNumArgOperands());
    } else {
      EXPECT_EQ(2u, NewCall->getNumArgOperands());
      EXPECT_EQ(ConstantInt::getFalse(C), NewCall->getArgOperand(1));
      EXPECT_EQ("count", NewCall->getName());
    }
    EXPECT_FALSE(verifyModule(M));
  }
}

TEST(IR, ObjectSizeCallUpgrade) {
  LLVMContext C;
  Module M("objectsize-upgrade", C);
  auto *FT = FunctionType::get(
      Type::getInt32Ty(C), {Type::getInt8PtrTy(C), Type::getInt1Ty(C)}, false);
  auto *Old = Function::Create(FT, GlobalValue::ExternalLinkage,
                               "llvm.objectsize.i32", &M);
  auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "f", &M);
  auto *BB = BasicBlock::Create(C, "entry", F);
  auto Arg = F->arg_begin();
  Value *Ptr = &*Arg++;
  Value *Min = &*Arg;
  auto *Call = CallInst::Create(Old, {Ptr, Min}, "size", BB);
  auto *Ret = ReturnInst::Create(C, Call, BB);
  UpgradeCallsToIntrinsic(Old);
  EXPECT_EQ(nullptr, M.getFunction("llvm.objectsize.i32"));
  auto *NewCall = llvm::cast<CallInst>(&BB->front());
  EXPECT_EQ(Intrinsic::objectsize,
            NewCall->getCalledFunction()->getIntrinsicID());
  EXPECT_EQ("llvm.objectsize.i32.p0i8",
            NewCall->getCalledFunction()->getName());
  EXPECT_EQ(2u, NewCall->getNumArgOperands());
  EXPECT_EQ(Ptr, NewCall->getArgOperand(0));
  EXPECT_EQ(Min, NewCall->getArgOperand(1));
  EXPECT_EQ("size", NewCall->getName());
  EXPECT_EQ(NewCall, Ret->getReturnValue());
  EXPECT_FALSE(verifyModule(M));
}

TEST(IR, DominanceAndStableBlockNumbers) {
  LLVMContext C;
  Module M("cfg", C);
  auto *Int32Ty = Type::getInt32Ty(C);
  auto *FT = FunctionType::get(Int32Ty, {Type::getInt1Ty(C)}, false);
  auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "diamond", &M);
  auto *Entry = BasicBlock::Create(C, "entry", F);
  auto *Left = BasicBlock::Create(C, "left", F);
  auto *Right = BasicBlock::Create(C, "right", F);
  auto *Merge = BasicBlock::Create(C, "merge", F);
  BranchInst::Create(Left, Right, &*F->arg_begin(), Entry);
  BranchInst::Create(Merge, Left);
  BranchInst::Create(Merge, Right);
  auto *Phi = PHINode::Create(Int32Ty, 2, "", Merge);
  Phi->addIncoming(ConstantInt::get(Int32Ty, 1), Left);
  Phi->addIncoming(ConstantInt::get(Int32Ty, 2), Right);
  ReturnInst::Create(C, Phi, Merge);

  DominatorTree DT;
  DT.recalculate(*F);
  EXPECT_TRUE(DT.dominates(Entry, Merge));
  EXPECT_FALSE(DT.dominates(Left, Merge));
  EXPECT_EQ(Entry, DT.getNode(Merge)->getIDom()->getBlock());
  EXPECT_TRUE(DT.verify());
  EXPECT_FALSE(verifyModule(M));

  unsigned MergeNumber = Merge->getNumber();
  auto *Dead = BasicBlock::Create(C, "dead", F, Left);
  ReturnInst::Create(C, ConstantInt::get(Int32Ty, 0), Dead);
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
  auto *Int32Ty = Type::getInt32Ty(C);
  auto *FT = FunctionType::get(Int32Ty, {Type::getInt1Ty(C), Int32Ty}, false);
  auto *F = Function::Create(FT, GlobalValue::ExternalLinkage, "broken", &M);
  auto *Entry = BasicBlock::Create(C, "entry", F);
  auto *Left = BasicBlock::Create(C, "left", F);
  auto *Right = BasicBlock::Create(C, "right", F);
  auto *Merge = BasicBlock::Create(C, "merge", F);
  auto Arg = F->arg_begin();
  Value *Condition = &*Arg++;
  Value *Input = &*Arg;
  BranchInst::Create(Left, Right, Condition, Entry);
  auto *Sum =
      BinaryOperator::CreateAdd(Input, ConstantInt::get(Int32Ty, 1), "", Left);
  BranchInst::Create(Merge, Left);
  BranchInst::Create(Merge, Right);
  ReturnInst::Create(C, Sum, Merge);

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
  auto *File = DIFile::get(C, "test.c", "/");
  auto *Ty = DIBasicType::get(C, llvm::dwarf::DW_TAG_base_type, "int", 32, 32,
                              llvm::dwarf::DW_ATE_signed);
  EXPECT_EQ(File, DIFile::get(C, "test.c", "/"));
  auto *SubTy = DISubroutineType::get(C, 0, MDTuple::get(C, {nullptr}));
  auto *SP = DISubprogram::get(C, DIScopeRef(File), "f", "f", File, 1, SubTy,
                               false, false, 1, nullptr, 0, 0, 0, false);
  auto *Local = DILocalVariable::get(C, DW_TAG_auto_variable, SP, "local", File,
                                     1, DITypeRef(Ty), 0, 0);
  auto *Arg = DILocalVariable::get(C, DW_TAG_arg_variable, SP, "arg", File, 1,
                                   DITypeRef(Ty), 1, 0);
  auto *CU =
      DICompileUnit::get(C, llvm::dwarf::DW_LANG_C99, File, "dxil-tools", false,
                         "", 0, "", 1, nullptr, MDTuple::get(C, {Ty}),
                         MDTuple::get(C, {SP}), nullptr, nullptr, 0);
  M.getOrInsertNamedMetadata("llvm.dbg.cu")->addOperand(CU);
  M.getOrInsertNamedMetadata("variables")->addOperand(Local);
  M.getOrInsertNamedMetadata("variables")->addOperand(Arg);
  EXPECT_EQ(1u, CU->getRetainedTypes().size());
  EXPECT_FALSE(verifyModule(M));
  std::string Text;
  llvm::raw_string_ostream OS(Text);
  M.print(OS, nullptr);
  EXPECT_NE(std::string::npos, Text.find("tag: DW_TAG_auto_variable"));
  EXPECT_NE(std::string::npos, Text.find("tag: DW_TAG_arg_variable"));
}

} // namespace
