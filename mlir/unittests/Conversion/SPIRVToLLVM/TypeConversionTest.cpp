//===- TypeConversionTest.cpp - SPIR-V to LLVM type conversion tests ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/SPIRVToLLVM/SPIRVToLLVM.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMTypes.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVDialect.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVTypes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/MLIRContext.h"

#include "gtest/gtest.h"

using namespace mlir;

namespace {

/// Fixture providing a type converter populated with the SPIR-V to LLVM type
/// conversions. These tests cover image types that cannot be spelled in the
/// textual format -- an image with a void sampled type is only produced when
/// deserializing an `OpTypeImage` whose sampled type is `OpTypeVoid` -- and so
/// are not reachable from the lit tests in test/Conversion/SPIRVToLLVM.
class SPIRVToLLVMTypeConversionTest : public ::testing::Test {
protected:
  SPIRVToLLVMTypeConversionTest() : converter(&context) {
    context.loadDialect<spirv::SPIRVDialect, LLVM::LLVMDialect>();
    populateSPIRVToLLVMTypeConversion(converter);
  }

  MLIRContext context;
  LLVMTypeConverter converter;
};

TEST_F(SPIRVToLLVMTypeConversionTest, VoidSampledType) {
  // The OpenCL `image2d_depth_ro_t` type, i.e. an `OpTypeImage` with a sampled
  // type of `OpTypeVoid`, which the SPIR-V dialect models as `NoneType`.
  auto imageType = spirv::ImageType::get(
      NoneType::get(&context), spirv::Dim::Dim2D,
      spirv::ImageDepthInfo::IsDepth, spirv::ImageArrayedInfo::NonArrayed,
      spirv::ImageSamplingInfo::SingleSampled,
      spirv::ImageSamplerUseInfo::SamplerUnknown, spirv::ImageFormat::Unknown);

  auto convertedType = dyn_cast_or_null<LLVM::LLVMTargetExtType>(
      converter.convertType(imageType));
  ASSERT_TRUE(convertedType);
  EXPECT_EQ(convertedType.getExtTypeName(), "spirv.Image");
  ASSERT_EQ(convertedType.getTypeParams().size(), 1u);
  EXPECT_TRUE(isa<LLVM::LLVMVoidType>(convertedType.getTypeParams()[0]));
  EXPECT_EQ(convertedType.getIntParams(),
            ArrayRef<unsigned>({1, 1, 0, 0, 0, 0}));
}

TEST_F(SPIRVToLLVMTypeConversionTest, VoidSampledTypeInSampledImage) {
  auto imageType = spirv::ImageType::get(
      NoneType::get(&context), spirv::Dim::Dim2D,
      spirv::ImageDepthInfo::NoDepth, spirv::ImageArrayedInfo::NonArrayed,
      spirv::ImageSamplingInfo::SingleSampled,
      spirv::ImageSamplerUseInfo::NeedSampler, spirv::ImageFormat::Unknown);
  auto sampledImageType = spirv::SampledImageType::get(imageType);

  auto convertedType = dyn_cast_or_null<LLVM::LLVMTargetExtType>(
      converter.convertType(sampledImageType));
  ASSERT_TRUE(convertedType);
  EXPECT_EQ(convertedType.getExtTypeName(), "spirv.SampledImage");
  ASSERT_EQ(convertedType.getTypeParams().size(), 1u);
  EXPECT_TRUE(isa<LLVM::LLVMVoidType>(convertedType.getTypeParams()[0]));
  EXPECT_EQ(convertedType.getIntParams(),
            ArrayRef<unsigned>({1, 0, 0, 0, 1, 0}));
}

TEST_F(SPIRVToLLVMTypeConversionTest, UnconvertibleSampledType) {
  // A sampled type the LLVM type converter has no mapping for must make the
  // whole image type fail to convert rather than produce a null parameter.
  auto imageType = spirv::ImageType::get(
      RankedTensorType::get({4}, Float32Type::get(&context)), spirv::Dim::Dim2D,
      spirv::ImageDepthInfo::NoDepth, spirv::ImageArrayedInfo::NonArrayed,
      spirv::ImageSamplingInfo::SingleSampled,
      spirv::ImageSamplerUseInfo::SamplerUnknown, spirv::ImageFormat::Unknown);

  EXPECT_FALSE(converter.convertType(imageType));
}

} // namespace
