//===- IRHeadersTest.cpp - Legacy IR and modern LLVM interoperability
//-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dxil/IR/Attributes.h"
#include "dxil/IR/LLVMContext.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/LLVMContext.h"
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
