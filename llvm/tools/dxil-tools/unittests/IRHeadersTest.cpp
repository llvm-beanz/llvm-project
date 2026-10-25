//===- IRHeadersTest.cpp - Legacy IR and modern LLVM interoperability
//-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/AssemblyAnnotationWriter.h"
#include "dxil/IR/Attributes.h"
#include "dxil/IR/CFG.h"
#include "dxil/IR/CallSite.h"
#include "dxil/IR/Comdat.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/DebugInfoMetadata.h"
#include "dxil/IR/DerivedTypes.h"
#include "dxil/IR/DiagnosticInfo.h"
#include "dxil/IR/DiagnosticPrinter.h"
#include "dxil/IR/Function.h"
#include "dxil/IR/GlobalValue.h"
#include "dxil/IR/InlineAsm.h"
#include "dxil/IR/InstIterator.h"
#include "dxil/IR/InstrTypes.h"
#include "dxil/IR/IntrinsicInst.h"
#include "dxil/IR/LLVMContext.h"
#include "dxil/IR/Metadata.h"
#include "dxil/IR/Module.h"
#include "dxil/IR/Statepoint.h"
#include "dxil/IR/TrackingMDRef.h"
#include "dxil/IR/TypeFinder.h"
#include "dxil/IR/User.h"
#include "dxil/IR/ValueHandle.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/TypeFinder.h"
#include "llvm/IR/User.h"
#include "llvm/IR/ValueHandle.h"
#include "gtest/gtest.h"
#include <type_traits>
#include <utility>

static_assert(!std::is_same_v<dxil::Attribute, llvm::Attribute>);
static_assert(!std::is_same_v<dxil::LLVMContext, llvm::LLVMContext>);
static_assert(
    std::is_same_v<decltype(std::declval<dxil::Attribute>().getKindAsString()),
                   llvm::StringRef>);
static_assert(
    std::is_same_v<decltype(&dxil::LLVMContext::getMDKindID),
                   unsigned (dxil::LLVMContext::*)(llvm::StringRef) const>);
static_assert(sizeof(dxil::AttributeSet) == sizeof(void *));
static_assert(!std::is_same_v<dxil::Type, llvm::Type>);
static_assert(!std::is_same_v<dxil::Value, llvm::Value>);
static_assert(!std::is_same_v<dxil::Use, llvm::Use>);
static_assert(!std::is_same_v<dxil::User, llvm::User>);
static_assert(!std::is_same_v<dxil::Comdat, llvm::Comdat>);
static_assert(!std::is_same_v<dxil::Function, llvm::Function>);
static_assert(!std::is_same_v<dxil::BasicBlock, llvm::BasicBlock>);
static_assert(!std::is_same_v<dxil::Instruction, llvm::Instruction>);
static_assert(!std::is_same_v<dxil::Metadata, llvm::Metadata>);
static_assert(!std::is_same_v<dxil::Module, llvm::Module>);
static_assert(!std::is_same_v<dxil::ConstantInt, llvm::ConstantInt>);
static_assert(!std::is_same_v<dxil::DILocation, llvm::DILocation>);
static_assert(!std::is_same_v<dxil::WeakTrackingVH, llvm::WeakTrackingVH>);
static_assert(!std::is_same_v<dxil::IntrinsicInst, llvm::IntrinsicInst>);
static_assert(!std::is_same_v<dxil::DiagnosticInfo, llvm::DiagnosticInfo>);
static_assert(!std::is_same_v<dxil::TypeFinder, llvm::TypeFinder>);
static_assert(!std::is_same_v<dxil::AssemblyAnnotationWriter,
                              llvm::AssemblyAnnotationWriter>);
static_assert(
    std::is_same_v<decltype(&dxil::AssemblyAnnotationWriter::printInfoComment),
                   void (dxil::AssemblyAnnotationWriter::*)(
                       const dxil::Value &, llvm::formatted_raw_ostream &)>);
static_assert(dxil::DW_TAG_auto_variable == 0x0100);
static_assert(dxil::DW_TAG_arg_variable == 0x0101);
static_assert(!std::is_abstract_v<dxil::DiagnosticPrinterRawOStream>);
static_assert(std::is_constructible_v<dxil::DiagnosticPrinterRawOStream,
                                      llvm::raw_ostream &>);
static_assert(std::is_same_v<decltype(dxil::ICmpInst::makeConstantRange(
                                 dxil::CmpInst::ICMP_EQ,
                                 std::declval<const llvm::APInt &>())),
                             llvm::ConstantRange>);
static_assert(
    std::is_same_v<decltype(std::declval<dxil::Instruction>().getNextNode()),
                   dxil::Instruction *>);
static_assert(std::is_same_v<
              decltype(std::declval<const dxil::BasicBlock>().getPrevNode()),
              const dxil::BasicBlock *>);
static_assert(
    std::is_same_v<llvm::simplify_type<dxil::WeakTrackingVH>::SimpleType,
                   dxil::Value *>);
static_assert(
    std::is_same_v<llvm::simplify_type<const dxil::DITypeRef>::SimpleType,
                   dxil::Metadata *>);
static_assert(std::is_same_v<llvm::GraphTraits<dxil::Function *>::NodeRef,
                             dxil::BasicBlock *>);
static_assert(std::is_same_v<llvm::GraphTraits<const dxil::Function *>::NodeRef,
                             const dxil::BasicBlock *>);
static_assert(
    std::is_same_v<std::remove_reference_t<
                       decltype(*std::declval<llvm::GraphTraits<
                                    dxil::Function *>::nodes_iterator>())>,
                   dxil::BasicBlock *>);
static_assert(std::is_same_v<
              std::remove_reference_t<
                  decltype(*std::declval<llvm::GraphTraits<
                               const dxil::Function *>::nodes_iterator>())>,
              const dxil::BasicBlock *>);
static_assert(std::is_same_v<
              llvm::GraphTraits<llvm::Inverse<dxil::BasicBlock *>>::NodeRef,
              dxil::BasicBlock *>);
static_assert(std::is_base_of_v<
              llvm::iplist_impl<llvm::simple_ilist<dxil::Instruction>,
                                dxil::SymbolTableListTraits<dxil::Instruction,
                                                            dxil::BasicBlock>>,
              dxil::BasicBlock::InstListType>);
static_assert(std::is_same_v<dxil::Function::BasicBlockListType::iterator,
                             llvm::simple_ilist<dxil::BasicBlock>::iterator>);
static_assert(
    std::is_same_v<dxil::Module::FunctionListType::const_iterator,
                   llvm::simple_ilist<dxil::Function>::const_iterator>);
static_assert(
    llvm::PointerLikeTypeTraits<dxil::Instruction *>::NumLowBitsAvailable == 2);
static_assert(
    std::is_same_v<llvm::simplify_type<dxil::TrackingMDRef>::SimpleType,
                   dxil::Metadata *>);
static_assert(
    std::is_same_v<llvm::simplify_type<dxil::TrackingMDNodeRef>::SimpleType,
                   dxil::MDNode *>);
static_assert(std::is_same_v<llvm::simplify_type<dxil::MDOperand>::SimpleType,
                             dxil::Metadata *>);
static_assert(
    std::is_same_v<llvm::simplify_type<dxil::Use>::SimpleType, dxil::Value *>);
static_assert(
    std::is_same_v<llvm::simplify_type<dxil::User::op_iterator>::SimpleType,
                   dxil::Value *>);
static_assert(
    std::is_same_v<llvm::GraphTraits<dxil::Type *>::NodeRef, dxil::Type *>);
static_assert(std::is_same_v<llvm::GraphTraits<const dxil::Type *>::NodeRef,
                             const dxil::Type *>);
static_assert(std::is_same_v<decltype(std::declval<dxil::Type>().subtypes()),
                             llvm::ArrayRef<dxil::Type *>>);
static_assert(std::is_same_v<decltype(std::declval<dxil::Value>().getName()),
                             llvm::StringRef>);
static_assert(std::is_same_v<decltype(std::declval<dxil::User>().operands()),
                             llvm::iterator_range<dxil::Use *>>);

static_assert(dxil::Type::X86_MMXTyID == 9);
static_assert(dxil::Type::IntegerTyID == 10);
static_assert(dxil::Type::PointerTyID == 14);
static_assert(dxil::Type::VectorTyID == 15);
static_assert(dxil::Instruction::Ret == 1);
static_assert(dxil::Instruction::FAdd == 9);
static_assert(dxil::Instruction::AtomicRMW == 32);
static_assert(dxil::Instruction::Call == 49);

namespace {

TEST(IRHeaders, EmptyCallSites) {
  dxil::CallSite Mutable;
  dxil::ImmutableCallSite Immutable;
  EXPECT_FALSE(Mutable);
  EXPECT_FALSE(Immutable);
  EXPECT_EQ(nullptr, Mutable.getInstruction());
  EXPECT_EQ(nullptr, Immutable.getInstruction());
  EXPECT_FALSE(Mutable.isCall());
  EXPECT_FALSE(Mutable.isInvoke());
  EXPECT_FALSE(Immutable.isCall());
  EXPECT_FALSE(Immutable.isInvoke());

  dxil::ImmutableCallSite Converted(Mutable);
  EXPECT_FALSE(Converted);
  EXPECT_FALSE(dxil::CallSite(static_cast<dxil::Value *>(nullptr)));
  EXPECT_FALSE(
      dxil::ImmutableCallSite(static_cast<const dxil::Value *>(nullptr)));
  EXPECT_EQ(Mutable, dxil::CallSite());
}

TEST(IRHeaders, IntrinsicArgumentDescriptorEncoding) {
  using Descriptor = dxil::Intrinsic::IITDescriptor;
  for (unsigned Argument : {0u, 1u, 7u, 31u}) {
    for (auto Kind : {Descriptor::AK_Any, Descriptor::AK_AnyInteger,
                      Descriptor::AK_AnyFloat, Descriptor::AK_AnyVector,
                      Descriptor::AK_AnyPointer}) {
      auto D = Descriptor::get(Descriptor::Argument, (Argument << 3) | Kind);
      EXPECT_EQ(Argument, D.getArgumentNumber());
      EXPECT_EQ(Kind, D.getArgumentKind());
    }
  }
}

TEST(IRHeaders, AttributeSetSupportsModernDenseMap) {
  llvm::DenseMap<dxil::AttributeSet, unsigned> Map;
  dxil::AttributeSet Empty;
  EXPECT_EQ(nullptr, Empty.getRawPointer());
  EXPECT_TRUE(Map.try_emplace(Empty, 42).second);
  EXPECT_FALSE(Map.try_emplace(dxil::AttributeSet(), 0).second);
  EXPECT_EQ(42u, Map.lookup(Empty));
  EXPECT_EQ(1u, Map.size());
  EXPECT_TRUE(Map.erase(Empty));
  EXPECT_TRUE(Map.empty());
}

TEST(IRHeaders, AAMDNodesSupportsModernDenseMap) {
  llvm::DenseMap<dxil::AAMDNodes, unsigned> Map;
  dxil::AAMDNodes Empty;
  EXPECT_TRUE(Map.try_emplace(Empty, 42).second);
  EXPECT_FALSE(Map.try_emplace(dxil::AAMDNodes(), 0).second);
  EXPECT_EQ(42u, Map.lookup(Empty));
  EXPECT_TRUE(Map.erase(Empty));
  EXPECT_TRUE(Map.empty());
}

} // namespace
