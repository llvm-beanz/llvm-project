//===--------------- DXSA.cpp - MLIR DXSA Operations ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Dialect/DXSA/IR/DXSA.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/OpImplementation.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Format.h"

using namespace mlir;
using namespace feme::dxsa;

#include "feme/Dialect/DXSA/IR/DXSAOpsDialect.cpp.inc"
#include "feme/Dialect/DXSA/IR/DXSAOpsEnums.cpp.inc"

void DXSADialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "feme/Dialect/DXSA/IR/DXSAOps.cpp.inc"
      >();
  addTypes<
#define GET_TYPEDEF_LIST
#include "feme/Dialect/DXSA/IR/DXSAOpsTypes.cpp.inc"
      >();
  registerAttributes();
}

/// Declarations for custom-directive helpers used by the
/// TableGen-generated print/parse methods.
static ParseResult parseHexTokens(OpAsmParser &parser, DenseI32ArrayAttr &attr);
static void printHexTokens(OpAsmPrinter &printer, Operation *,
                           DenseI32ArrayAttr attr);

//===----------------------------------------------------------------------===//
// DclThreadGroup
//===----------------------------------------------------------------------===//

LogicalResult DclThreadGroup::verify() {
  constexpr int64_t maxTotalThreads = 1024;
  if (auto total = getX() * getY() * getZ(); total > maxTotalThreads)
    return emitOpError("thread group size x*y*z must be <= ")
           << maxTotalThreads << ", got " << total;
  return success();
}

//===----------------------------------------------------------------------===//
// TableGen'd op method definitions
//===----------------------------------------------------------------------===//

#define GET_OP_CLASSES
#include "feme/Dialect/DXSA/IR/DXSAOps.cpp.inc"

//===----------------------------------------------------------------------===//
// ModuleOp
//===----------------------------------------------------------------------===//

ParseResult ModuleOp::parse(OpAsmParser &parser, OperationState &result) {
  // Parse optional shader information like `pixel_shader 5 0`.
  StringRef typeKeyword;
  auto typeLoc = parser.getCurrentLocation();
  if (succeeded(parser.parseOptionalKeyword(&typeKeyword))) {
    auto programType = symbolizeProgramType(typeKeyword);
    if (!programType)
      return parser.emitError(typeLoc)
             << "unknown program type: " << typeKeyword;
    result.addAttribute("program_type", ProgramTypeAttr::get(
                                            parser.getContext(), *programType));
    auto intType = parser.getBuilder().getI32Type();
    IntegerAttr major, minor;
    if (parser.parseAttribute(major, intType, "major_version",
                              result.attributes) ||
        parser.parseAttribute(minor, intType, "minor_version",
                              result.attributes))
      return failure();
  }

  Region *body = result.addRegion();
  if (parser.parseOptionalAttrDictWithKeyword(result.attributes) ||
      parser.parseRegion(*body, /*arguments=*/{}))
    return failure();

  if (body->empty())
    body->push_back(new Block());

  return success();
}

void ModuleOp::print(OpAsmPrinter &printer) {
  if (auto programType = getProgramType()) {
    printer << ' ' << stringifyProgramType(*programType);
    printer << ' ' << *getMajorVersion() << ' ' << *getMinorVersion();
  }
  printer.printOptionalAttrDictWithKeyword(
      (*this)->getAttrs(), {"program_type", "major_version", "minor_version"});
  printer << ' ';
  printer.printRegion(getBody());
}

LogicalResult ModuleOp::verify() {
  bool hasType = static_cast<bool>(getProgramTypeAttr());
  bool hasMajor = static_cast<bool>(getMajorVersionAttr());
  bool hasMinor = static_cast<bool>(getMinorVersionAttr());
  bool allPresent = hasType && hasMajor && hasMinor;
  bool allAbsent = !hasType && !hasMajor && !hasMinor;
  if (!allPresent && !allAbsent)
    return emitOpError("program_type, major_version and minor_version must all "
                       "be present or all absent");
  return success();
}

//===----------------------------------------------------------------------===//
// Op verifiers
//===----------------------------------------------------------------------===//

LogicalResult DclIndexRange::verify() {
  auto operandType = getOperand().getType();
  if (operandType != OperandType::v && operandType != OperandType::o &&
      operandType != OperandType::vpc)
    return emitOpError(
               "operand must be an input, output or patch-constant register, "
               "got ")
           << stringifyOperandType(operandType);
  return success();
}

LogicalResult DclHsMaxTessFactor::verify() {
  auto value = getMaxTessfactorAttr().getValue();
  if (!value.isFinite() || value < llvm::APFloat(1.0f) ||
      value > llvm::APFloat(64.0f))
    return emitOpError("MaxTessFactor must be in [1.0, 64.0], got ")
           << value.convertToFloat();
  return success();
}

LogicalResult DclTgsmRaw::verify() {
  auto byteCount = getByteCount();
  if (byteCount % 4 != 0)
    return emitOpError("byte count must be a multiple of 4, got ") << byteCount;
  return success();
}

LogicalResult DclTgsmStructured::verify() {
  auto stride = getStructByteStride();
  auto count = getStructCount();
  if (stride % 4 != 0)
    return emitOpError("struct byte stride must be a multiple of 4, got ")
           << stride;
  auto totalSize = static_cast<uint64_t>(stride) * count;
  if (totalSize > 32768)
    return emitOpError("total size struct_byte_stride * struct_count must "
                       "be <= 32768, got ")
           << totalSize;
  return success();
}

LogicalResult DclConstantBuffer::verify() {
  auto lbound = getLbound();
  auto ubound = getUbound();
  if (lbound && ubound && *lbound > *ubound)
    return emitOpError("expected lbound <= ubound, got lbound=")
           << *lbound << ", ubound=" << *ubound;
  return success();
}

LogicalResult DclSampler::verify() {
  auto lbound = getLbound();
  auto ubound = getUbound();
  if (lbound && ubound && *lbound > *ubound)
    return emitOpError("expected lbound <= ubound, got lbound=")
           << *lbound << ", ubound=" << *ubound;
  return success();
}

static LogicalResult verifyLboundUboundSpace(Operation *op,
                                             std::optional<uint32_t> lbound,
                                             std::optional<uint32_t> ubound,
                                             std::optional<uint32_t> space) {
  if ((lbound || ubound || space) && !(lbound && ubound && space))
    return op->emitOpError(
        "lbound, ubound and space must be either all set or all absent");
  if (lbound && ubound && *lbound > *ubound)
    return op->emitOpError("expected lbound <= ubound, got lbound=")
           << *lbound << ", ubound=" << *ubound;
  return success();
}

LogicalResult DclResource::verify() {
  auto dim = getDim();
  bool isMultisampled = dim == ResourceDimension::texture2dms ||
                        dim == ResourceDimension::texture2dmsarray;
  if (isMultisampled && !getSampleCount())
    return emitOpError("missing sample_count for multisampled dimension ")
           << stringifyResourceDimension(dim);
  if (!isMultisampled && getSampleCount())
    return emitOpError("sample_count is only valid for texture2dms and "
                       "texture2dmsarray, got ")
           << stringifyResourceDimension(dim);
  return verifyLboundUboundSpace(*this, getLbound(), getUbound(), getSpace());
}

LogicalResult DclResourceRaw::verify() {
  return verifyLboundUboundSpace(*this, getLbound(), getUbound(), getSpace());
}

LogicalResult DclResourceStructured::verify() {
  auto stride = getStructByteStride();
  if (stride % 4 != 0)
    return emitOpError("struct byte stride must be a multiple of 4, got ")
           << stride;
  return verifyLboundUboundSpace(*this, getLbound(), getUbound(), getSpace());
}

static LogicalResult
verifyNoOrderPreservingCounter(Operation *op, std::optional<UAVFlags> flags) {
  if (flags && bitEnumContainsAny(*flags, UAVFlags::hasOrderPreservingCounter))
    return op->emitOpError(
        "hasOrderPreservingCounter flag is only valid for dcl_uav_structured");
  return success();
}

LogicalResult DclUavTyped::verify() {
  auto dim = getDim();
  switch (dim) {
  case ResourceDimension::buffer:
  case ResourceDimension::texture1d:
  case ResourceDimension::texture1darray:
  case ResourceDimension::texture2d:
  case ResourceDimension::texture2darray:
  case ResourceDimension::texture3d:
    break;
  default:
    return emitOpError("invalid dimension for typed UAV: ")
           << stringifyResourceDimension(dim);
  }
  if (failed(verifyNoOrderPreservingCounter(*this, getFlags())))
    return failure();
  return verifyLboundUboundSpace(*this, getLbound(), getUbound(), getSpace());
}

LogicalResult DclUavRaw::verify() {
  if (failed(verifyNoOrderPreservingCounter(*this, getFlags())))
    return failure();
  return verifyLboundUboundSpace(*this, getLbound(), getUbound(), getSpace());
}

LogicalResult DclUavStructured::verify() {
  auto stride = getStructByteStride();
  if (stride % 4 != 0)
    return emitOpError("struct byte stride must be a multiple of 4, got ")
           << stride;
  return verifyLboundUboundSpace(*this, getLbound(), getUbound(), getSpace());
}

LogicalResult Sync::verify() {
  if (bitEnumContainsAll(getFlags(),
                         SyncFlags::uav_global | SyncFlags::uav_group))
    return emitOpError("uav_global and uav_group are mutually exclusive");
  return success();
}

//===----------------------------------------------------------------------===//
// UnknownOp
//===----------------------------------------------------------------------===//

LogicalResult Unknown::verify() {
  if (getTokens().empty())
    return emitOpError("tokens must not be empty");
  return success();
}

/// Parse `<tokens = [0x..., ...]>` for the unknown op.
static ParseResult parseHexTokens(OpAsmParser &parser,
                                  DenseI32ArrayAttr &attr) {
  SmallVector<int32_t> tokens;
  auto parseOneToken = [&]() -> ParseResult {
    uint32_t value;
    if (parser.parseInteger(value))
      return failure();
    tokens.push_back(static_cast<int32_t>(value));
    return success();
  };

  if (parser.parseLess() || parser.parseKeyword("tokens") ||
      parser.parseEqual() ||
      parser.parseCommaSeparatedList(OpAsmParser::Delimiter::Square,
                                     parseOneToken) ||
      parser.parseGreater())
    return failure();

  attr = DenseI32ArrayAttr::get(parser.getContext(), tokens);
  return success();
}

/// Print the tokens array as uppercase, 8-digit, 0x-prefixed hex.
static void printHexTokens(OpAsmPrinter &printer, Operation *,
                           DenseI32ArrayAttr attr) {
  printer << "<tokens = [";
  llvm::interleaveComma(attr.asArrayRef(), printer.getStream(), [&](int32_t t) {
    printer.getStream() << llvm::format_hex(static_cast<uint32_t>(t),
                                            /*Width=*/10, /*Upper=*/true);
  });
  printer << "]>";
}

//===----------------------------------------------------------------------===//
// TableGen'd type method definitions
//===----------------------------------------------------------------------===//

#define GET_TYPEDEF_CLASSES
#include "feme/Dialect/DXSA/IR/DXSAOpsTypes.cpp.inc"
