//===-- LayoutUtils.cpp - Decorate composite type with layout information -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements Utilities used to get alignment and layout information
// for types in SPIR-V dialect.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/SPIRV/Utils/LayoutUtils.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVTypes.h"
#include "llvm/Support/MathExtras.h"

using namespace mlir;

spirv::StructType
VulkanLayoutUtils::decorateType(spirv::StructType structType) {
  Size size = 0;
  Size alignment = 1;
  return decorateType(structType, size, alignment);
}

spirv::StructType
VulkanLayoutUtils::decorateType(spirv::StructType structType,
                                VulkanLayoutUtils::Size &size,
                                VulkanLayoutUtils::Size &alignment) {
  if (structType.getNumElements() == 0) {
    return structType;
  }

  SmallVector<Type, 4> memberTypes;
  SmallVector<spirv::StructType::OffsetInfo, 4> offsetInfo;
  SmallVector<spirv::StructType::MemberDecorationInfo, 4> memberDecorations;

  Size structMemberOffset = 0;
  Size maxMemberAlignment = 1;

  for (uint32_t i = 0, e = structType.getNumElements(); i < e; ++i) {
    Size memberSize = 0;
    Size memberAlignment = 1;

    Type memberType =
        decorateType(structType.getElementType(i), memberSize, memberAlignment);
    // A member that itself cannot be decorated (e.g. a nested identified
    // struct -- see the `nullptr` return below) leaves this struct equally
    // undecoratable; propagate the failure instead of building a struct with
    // a null member type, which every one of `StructType::get`'s own callers
    // (its verifier included) assumes never happens.
    if (!memberType)
      return nullptr;
    structMemberOffset = llvm::alignTo(structMemberOffset, memberAlignment);
    memberTypes.push_back(memberType);
    offsetInfo.push_back(
        static_cast<spirv::StructType::OffsetInfo>(structMemberOffset));
    // If the member's size is the max value, it must be the last member and it
    // must be a runtime array.
    assert(memberSize != std::numeric_limits<Size>().max() ||
           (i + 1 == e &&
            isa<spirv::RuntimeArrayType>(structType.getElementType(i))));
    // According to the Vulkan spec:
    // "A structure has a base alignment equal to the largest base alignment of
    // any of its members."
    structMemberOffset += memberSize;
    maxMemberAlignment = std::max(maxMemberAlignment, memberAlignment);
  }

  // According to the Vulkan spec:
  // "The Offset decoration of a member must not place it between the end of a
  // structure or an array and the next multiple of the alignment of that
  // structure or array."
  size = llvm::alignTo(structMemberOffset, maxMemberAlignment);
  alignment = maxMemberAlignment;
  structType.getMemberDecorations(memberDecorations);

  if (!structType.isIdentified())
    return spirv::StructType::get(memberTypes, offsetInfo, memberDecorations);

  // Identified structs are uniqued by identifier so it is not possible
  // to create 2 structs with the same name but different decorations.
  return nullptr;
}

Type VulkanLayoutUtils::decorateType(Type type, VulkanLayoutUtils::Size &size,
                                     VulkanLayoutUtils::Size &alignment) {
  if (isa<spirv::ScalarType>(type)) {
    alignment = getScalarTypeAlignment(type);
    // Vulkan spec does not specify any padding for a scalar type.
    size = alignment;
    return type;
  }
  if (auto structType = dyn_cast<spirv::StructType>(type))
    return decorateType(structType, size, alignment);
  if (auto arrayType = dyn_cast<spirv::ArrayType>(type))
    return decorateType(arrayType, size, alignment);
  if (auto vectorType = dyn_cast<VectorType>(type))
    return decorateType(vectorType, size, alignment);
  if (auto matrixType = dyn_cast<spirv::MatrixType>(type))
    return decorateType(matrixType, size, alignment);
  if (auto arrayType = dyn_cast<spirv::RuntimeArrayType>(type)) {
    size = std::numeric_limits<Size>().max();
    return decorateType(arrayType, alignment);
  }
  if (isa<spirv::PointerType>(type)) {
    // TODO: Add support for `PhysicalStorageBufferAddresses`.
    return nullptr;
  }
  llvm_unreachable("unhandled SPIR-V type");
}

Type VulkanLayoutUtils::decorateType(VectorType vectorType,
                                     VulkanLayoutUtils::Size &size,
                                     VulkanLayoutUtils::Size &alignment) {
  const unsigned numElements = vectorType.getNumElements();
  Type elementType = vectorType.getElementType();
  Size elementSize = 0;
  Size elementAlignment = 1;

  Type memberType = decorateType(elementType, elementSize, elementAlignment);
  // A vector's element type is always an `spirv::ScalarType` per the SPIR-V
  // spec, which `decorateType` never fails to decorate -- but guard anyway,
  // symmetrically with the array/struct cases below, rather than relying on
  // that invariant silently continuing to hold.
  if (!memberType)
    return nullptr;
  // According to the Vulkan spec:
  // 1. "A two-component vector has a base alignment equal to twice its scalar
  // alignment."
  // 2. "A three- or four-component vector has a base alignment equal to four
  // times its scalar alignment."
  size = elementSize * numElements;
  alignment = numElements == 2 ? elementAlignment * 2 : elementAlignment * 4;
  return VectorType::get(numElements, memberType);
}

Type VulkanLayoutUtils::decorateType(spirv::ArrayType arrayType,
                                     VulkanLayoutUtils::Size &size,
                                     VulkanLayoutUtils::Size &alignment) {
  const unsigned numElements = arrayType.getNumElements();
  Type elementType = arrayType.getElementType();
  Size elementSize = 0;
  Size elementAlignment = 1;

  Type memberType = decorateType(elementType, elementSize, elementAlignment);
  // An element type that cannot itself be decorated (e.g. a nested
  // identified struct -- see the `nullptr` return in the struct overload)
  // leaves the whole array equally undecoratable; propagate the failure
  // instead of building an `ArrayType` with a null element type.
  if (!memberType)
    return nullptr;
  // According to the Vulkan spec:
  // "An array has a base alignment equal to the base alignment of its element
  // type."
  alignment = elementAlignment;
  // The stride between elements must be a multiple of the array's own
  // alignment (e.g. 16 bytes, not 12, for an array of 3-component vectors of
  // 4-byte scalars), even though the element's own compact size can be
  // smaller than that.
  Size stride = llvm::alignTo(elementSize, elementAlignment);
  // The array's own total size -- used to place any struct member that
  // follows it -- must therefore also be computed from that per-element
  // stride, not from the (possibly smaller) compact element size.
  size = stride * numElements;
  return spirv::ArrayType::get(memberType, numElements, stride);
}

std::optional<VulkanLayoutUtils::Size>
VulkanLayoutUtils::getNaturalArrayStride(Type elementType) {
  Size elementSize = 0;
  Size elementAlignment = 1;
  if (!decorateType(elementType, elementSize, elementAlignment))
    return std::nullopt;
  return llvm::alignTo(elementSize, elementAlignment);
}

Type VulkanLayoutUtils::decorateType(spirv::MatrixType matrixType,
                                     VulkanLayoutUtils::Size &size,
                                     VulkanLayoutUtils::Size &alignment) {
  const unsigned numColumns = matrixType.getNumColumns();
  Type columnType = matrixType.getColumnType();
  unsigned numElements = matrixType.getNumElements();
  Type elementType = matrixType.getElementType();
  Size elementSize = 0;
  Size elementAlignment = 1;

  Type memberType = decorateType(elementType, elementSize, elementAlignment);
  // A matrix's element type is always an `spirv::ScalarType`, which never
  // fails to decorate -- guarded anyway, symmetrically with the vector case
  // above.
  if (!memberType)
    return nullptr;
  // According to the Vulkan spec:
  // "A matrix type inherits scalar alignment from the equivalent array
  // declaration."
  size = elementSize * numElements;
  alignment = elementAlignment;
  return spirv::MatrixType::get(columnType, numColumns);
}

Type VulkanLayoutUtils::decorateType(spirv::RuntimeArrayType arrayType,
                                     VulkanLayoutUtils::Size &alignment) {
  Type elementType = arrayType.getElementType();
  Size elementSize = 0;

  Type memberType = decorateType(elementType, elementSize, alignment);
  // An element type that cannot itself be decorated (e.g. a nested
  // identified struct) leaves the whole runtime array equally
  // undecoratable; propagate the failure instead of building a
  // `RuntimeArrayType` with a null element type.
  if (!memberType)
    return nullptr;
  return spirv::RuntimeArrayType::get(memberType, elementSize);
}

VulkanLayoutUtils::Size
VulkanLayoutUtils::getScalarTypeAlignment(Type scalarType) {
  // According to the Vulkan spec:
  // 1. "A scalar of size N has a scalar alignment of N."
  // 2. "A scalar has a base alignment equal to its scalar alignment."
  // 3. "A scalar, vector or matrix type has an extended alignment equal to its
  // base alignment."
  unsigned bitWidth = scalarType.getIntOrFloatBitWidth();
  if (bitWidth == 1)
    return 1;
  return bitWidth / 8;
}

bool VulkanLayoutUtils::isLegalType(Type type) {
  auto ptrType = dyn_cast<spirv::PointerType>(type);
  if (!ptrType) {
    return true;
  }

  const spirv::StorageClass storageClass = ptrType.getStorageClass();
  auto structType = dyn_cast<spirv::StructType>(ptrType.getPointeeType());
  if (!structType) {
    return true;
  }

  switch (storageClass) {
  case spirv::StorageClass::Uniform:
  case spirv::StorageClass::StorageBuffer:
  case spirv::StorageClass::PushConstant:
  case spirv::StorageClass::PhysicalStorageBuffer:
    return structType.hasOffset() || !structType.getNumElements();
  default:
    return true;
  }
}
