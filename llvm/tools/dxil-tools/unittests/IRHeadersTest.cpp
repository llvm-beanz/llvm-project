//===- IRHeadersTest.cpp - Legacy IR and modern LLVM interoperability
//-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/Attributes.h"
#include "dxil/IR/Comdat.h"
#include "dxil/IR/DerivedTypes.h"
#include "dxil/IR/GlobalValue.h"
#include "dxil/IR/InlineAsm.h"
#include "dxil/IR/LLVMContext.h"
#include "dxil/IR/User.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/User.h"
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

namespace {

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

} // namespace
