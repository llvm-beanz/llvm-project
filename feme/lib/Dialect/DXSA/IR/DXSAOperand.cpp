//===--------- DXSAOperand.cpp - DXSA operand attributes ------------------===//
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
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"

#include <cmath>
#include <functional>
#include <optional>
#include <type_traits>

using namespace mlir;
using namespace feme::dxsa;

//===----------------------------------------------------------------------===//
// TableGen'd attribute method definitions
//===----------------------------------------------------------------------===//

static OperandComponents defaultComponentsFor(OperandType type);
static std::optional<OperandComponents> immComponentsFor(OperandType type,
                                                         size_t count);

#define GET_ATTRDEF_CLASSES
#include "feme/Dialect/DXSA/IR/DXSAOpsAttributes.cpp.inc"

void DXSADialect::registerAttributes() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "feme/Dialect/DXSA/IR/DXSAOpsAttributes.cpp.inc"
      >();
}

template <typename OperandAttrT>
static Attribute parseQualifiedOperandAttr(DialectAsmParser &parser,
                                           Type type) {
  if (parser.parseLess())
    return {};
  auto attr = OperandAttrT::parse(parser, type);
  if (!attr || parser.parseGreater())
    return {};
  return attr;
}

Attribute DXSADialect::parseAttribute(DialectAsmParser &parser,
                                      Type type) const {
  auto typeLoc = parser.getCurrentLocation();
  if (succeeded(parser.parseOptionalKeyword(DstOperandAttr::getMnemonic())))
    return parseQualifiedOperandAttr<DstOperandAttr>(parser, type);
  if (succeeded(parser.parseOptionalKeyword(SrcOperandAttr::getMnemonic())))
    return parseQualifiedOperandAttr<SrcOperandAttr>(parser, type);

  StringRef mnemonic;
  Attribute attr;
  if (auto result = generatedAttributeParser(parser, &mnemonic, type, attr);
      result.has_value())
    return attr;
  parser.emitError(typeLoc) << "unknown attribute `" << mnemonic
                            << "` in dialect `" << getNamespace() << "`";
  return {};
}

void DXSADialect::printAttribute(Attribute attr,
                                 DialectAsmPrinter &printer) const {
  if (auto dst = llvm::dyn_cast<DstOperandAttr>(attr)) {
    printer << DstOperandAttr::getMnemonic() << '<';
    dst.print(printer);
    printer << '>';
    return;
  }
  if (auto src = llvm::dyn_cast<SrcOperandAttr>(attr)) {
    printer << SrcOperandAttr::getMnemonic() << '<';
    src.print(printer);
    printer << '>';
    return;
  }
  if (failed(generatedAttributePrinter(attr, printer)))
    llvm_unreachable("DXSA dialect has no printer for this attribute");
}

//===----------------------------------------------------------------------===//
// Immediate-literal codec for `l(...)` / `d(...)` payloads
//===----------------------------------------------------------------------===//

static std::optional<OperandComponents> immComponentsFor(OperandType type,
                                                         size_t count) {
  switch (type) {
  case OperandType::l:
    if (count == 1)
      return OperandComponents::scalar;
    if (count == 4)
      return OperandComponents::vector;
    return std::nullopt;
  case OperandType::d:
    if (count == 2)
      return OperandComponents::vector;
    return std::nullopt;
  default:
    llvm_unreachable("non-immediate operand type");
  }
}

static FailureOr<uint64_t> parseImmValue(AsmParser &parser, unsigned width) {
  auto loc = parser.getCurrentLocation();
  // Prevent true/false as a valid arguments for the parseOptionalInteger.
  if (succeeded(parser.parseOptionalKeyword("true")) ||
      succeeded(parser.parseOptionalKeyword("false")))
    return parser.emitError(loc, "expected integer literal");
  APInt value;
  auto parsed = parser.parseOptionalInteger(value);
  if (!parsed.has_value())
    return parser.emitError(loc, "expected integer literal");
  if (failed(*parsed))
    return failure();
  // Reject type annotation.
  if (succeeded(parser.parseOptionalColon()))
    return parser.emitError(loc, "immediate literal cannot carry a type");
  auto neededBits = value.isNonNegative() ? value.getActiveBits()
                                          : value.getSignificantBits();
  if (neededBits > width)
    return parser.emitError(loc)
           << neededBits << "-bit immediate does not fit in " << width
           << "-bit literal";
  return value.sextOrTrunc(width).getZExtValue();
}

template <typename ImmT, typename AttrT>
static ParseResult parseImmValues(AsmParser &parser, AttrT &values) {
  constexpr unsigned bitWidth = sizeof(ImmT) * 8;
  SmallVector<ImmT> bits;
  if (parser.parseCommaSeparatedList(
          AsmParser::Delimiter::Paren, [&]() -> ParseResult {
            auto value = parseImmValue(parser, bitWidth);
            if (failed(value))
              return failure();
            bits.push_back(static_cast<ImmT>(*value));
            return success();
          }))
    return failure();
  values = AttrT::get(parser.getContext(), bits);
  return success();
}

static ParseResult parseImm32Body(AsmParser &parser,
                                  DenseI32ArrayAttr &values32) {
  return parseImmValues<int32_t>(parser, values32);
}

static ParseResult parseImm64Body(AsmParser &parser,
                                  DenseI64ArrayAttr &values64) {
  return parseImmValues<int64_t>(parser, values64);
}

/// Prints an immediate payload, formatting each element's raw bit pattern
/// as a 0x-prefixed uppercase hex literal.
template <typename ImmT>
static void printImmValues(AsmPrinter &printer, StringRef keyword,
                           ArrayRef<ImmT> values) {
  printer << keyword << "(";
  llvm::interleaveComma(values, printer.getStream(), [&](ImmT v) {
    printer.getStream() << llvm::format_hex(
        static_cast<std::make_unsigned_t<ImmT>>(v), /*Width=*/2,
        /*Upper=*/true);
  });
  printer << ")";
}

static void printImm32(AsmPrinter &printer, ArrayRef<int32_t> values) {
  printImmValues(printer, "l", values);
}

static void printImm64(AsmPrinter &printer, ArrayRef<int64_t> values64) {
  printImmValues(printer, "d", values64);
}

//===----------------------------------------------------------------------===//
// Helpers for operand attributes custom asm
//===----------------------------------------------------------------------===//

static constexpr StringLiteral nonUniformKeyword = "nonuniform";

/// Returns the default components for the given operand type.
/// Printing omits the explicit keyword when the value matches this default.
static OperandComponents defaultComponentsFor(OperandType type) {
  switch (type) {
  case OperandType::r:
  case OperandType::v:
  case OperandType::o:
  case OperandType::x:
  case OperandType::vicp:
  case OperandType::vocp:
  case OperandType::vpc:
  case OperandType::vDomain:
  case OperandType::vThreadID:
  case OperandType::vThreadGroupID:
  case OperandType::vThreadIDInGroup:
  case OperandType::cycleCounter:
    return OperandComponents::vector;
  case OperandType::l:
  case OperandType::d:
  case OperandType::oDepth:
  case OperandType::oDepthGE:
  case OperandType::oDepthLE:
  case OperandType::oMask:
  case OperandType::oStencilRef:
  case OperandType::vCoverage:
  case OperandType::vGSInstanceID:
  case OperandType::vForkInstanceID:
  case OperandType::vJoinInstanceID:
  case OperandType::vOutputControlPointID:
  case OperandType::vInnerCoverage:
  case OperandType::vPrim:
  case OperandType::vThreadIDInGroupFlattened:
    return OperandComponents::scalar;
  case OperandType::s:
  case OperandType::t:
  case OperandType::cb:
  case OperandType::icb:
  case OperandType::u:
  case OperandType::g:
  case OperandType::m:
  case OperandType::label:
  case OperandType::null:
  case OperandType::rasterizer:
  case OperandType::fb:
  case OperandType::ft:
  case OperandType::fp:
  case OperandType::funcInput:
  case OperandType::funcOutput:
  case OperandType::thisPtr:
    return OperandComponents::none;
  }
  llvm_unreachable("unknown operand type");
}

static bool isImmediateType(OperandType type) {
  return type == OperandType::l || type == OperandType::d;
}

static char charFromComponent(unsigned component) {
  switch (component) {
  case 0:
    return 'x';
  case 1:
    return 'y';
  case 2:
    return 'z';
  case 3:
    return 'w';
  default:
    llvm_unreachable("swizzle component out of [0, 3] range");
  }
}

/// Parses a single component keyword `x`, `y`, `z` or `w` into `component`.
static ParseResult parseComponent(AsmParser &parser, unsigned &component) {
  StringRef keyword;
  if (parser.parseKeyword(&keyword))
    return failure();
  component = llvm::StringSwitch<unsigned>(keyword)
                  .Case("x", 0)
                  .Case("y", 1)
                  .Case("z", 2)
                  .Case("w", 3)
                  .Default(4);
  if (component > 3)
    return parser.emitError(parser.getCurrentLocation())
           << "unknown component: `" << keyword << "`";
  return success();
}

static OptionalParseResult
parseOptionalComponents(AsmParser &parser,
                        SmallVectorImpl<unsigned> &components) {
  if (failed(parser.parseOptionalLess()))
    return std::nullopt;
  return failure(
      parser.parseCommaSeparatedList(AsmParser::Delimiter::None,
                                     [&]() -> ParseResult {
                                       unsigned component;
                                       if (parseComponent(parser, component))
                                         return failure();
                                       components.push_back(component);
                                       return success();
                                     }) ||
      parser.parseGreater());
}

static ParseResult parseComponents(AsmParser &parser,
                                   SmallVectorImpl<unsigned> &components) {
  OptionalParseResult result = parseOptionalComponents(parser, components);
  if (!result.has_value())
    return parser.emitError(parser.getCurrentLocation()) << "expected '<'";
  return *result;
}

//===----------------------------------------------------------------------===//
// SwizzleAttr
//===----------------------------------------------------------------------===//

LogicalResult SwizzleAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                  ArrayRef<unsigned> components) {
  if (components.size() != 1 && components.size() != 4)
    return emitError() << "swizzle must have 1 or 4 components, got "
                       << components.size();
  for (unsigned component : components) {
    if (component > 3)
      return emitError() << "component must be in [0, 3], got " << component;
  }
  return success();
}

Attribute SwizzleAttr::parse(AsmParser &parser, Type) {
  auto loc = parser.getCurrentLocation();
  SmallVector<unsigned, 4> components;
  if (parseComponents(parser, components))
    return {};
  return parser.getChecked<SwizzleAttr>(loc, parser.getContext(), components);
}

void SwizzleAttr::print(AsmPrinter &printer) const {
  printer << '<';
  llvm::interleaveComma(getComponents(), printer.getStream(),
                        [&](unsigned c) { printer << charFromComponent(c); });
  printer << '>';
}

//===----------------------------------------------------------------------===//
// IndexAttr
//===----------------------------------------------------------------------===//

LogicalResult IndexAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                                IntegerAttr imm, SrcOperandAttr relative) {
  if (!imm && !relative)
    return emitError() << "index must be either immediate or relative";
  if (imm) {
    auto type = imm.getType();
    if (!type.isInteger(32) && !type.isInteger(64))
      return emitError() << "unsupported index type: " << type;
  }
  return success();
}

/// Parses the tail of an index entry whose leading integer `value` was already
/// parsed by the caller, i.e. the optional `: type` width and `+ relative`
/// suffix (e.g. `2`, `2 : i64`, `2 : i64 + r<...>`).
static IndexAttr parseImmIndexEntryTail(AsmParser &parser, MLIRContext *ctx,
                                        SMLoc loc, uint64_t value) {
  IntegerType immType = IntegerType::get(ctx, 32);
  auto typeLoc = parser.getCurrentLocation();
  if (succeeded(parser.parseOptionalColon())) {
    typeLoc = parser.getCurrentLocation();
    if (parser.parseType(immType))
      return {};
  }

  // The width is taken as i32 by default. A value that does not fit must be
  // spelled with an explicit `: i64`. Widths other than i32/i64 are rejected
  // later by the verifier.
  if (immType.getWidth() < 64 && !llvm::isUIntN(immType.getWidth(), value)) {
    parser.emitError(typeLoc) << value << " does not fit in " << immType;
    return {};
  }
  auto immAttr = IntegerAttr::get(immType, value);

  SrcOperandAttr relative;
  if (succeeded(parser.parseOptionalPlus()))
    if (parser.parseCustomAttributeWithFallback(relative))
      return {};
  return parser.getChecked<IndexAttr>(loc, ctx, immAttr, relative);
}

Attribute IndexAttr::parse(AsmParser &parser, Type) {
  auto loc = parser.getCurrentLocation();
  auto *ctx = parser.getContext();

  uint64_t value;
  auto intResult = parser.parseOptionalInteger(value);
  if (intResult.has_value()) {
    if (failed(*intResult))
      return {};
    return parseImmIndexEntryTail(parser, ctx, loc, value);
  }

  SrcOperandAttr relative;
  if (parser.parseCustomAttributeWithFallback(relative))
    return {};
  return parser.getChecked<IndexAttr>(loc, ctx, IntegerAttr(), relative);
}

void IndexAttr::print(AsmPrinter &printer) const {
  if (auto imm = getImm()) {
    printer << imm.getValue();
    if (imm.getType().isInteger(64))
      printer << " : i64";
  }
  if (auto relative = getRelative()) {
    if (getImm())
      printer << " + ";
    printer.printStrippedAttrOrType(relative);
  }
}

//===----------------------------------------------------------------------===//
// Shared helpers for DstOperandAttr / SrcOperandAttr asm
//===----------------------------------------------------------------------===//

namespace {

struct DstOperandBody {
  OperandIndexAttr index;
  OperandComponentsAttr components;
  ComponentMaskAttr mask;
  OperandMinPrecisionAttr minPrecision;
};

struct SrcOperandBody {
  OperandIndexAttr index;
  OperandComponentsAttr components;
  SwizzleAttr swizzle;
  OperandMinPrecisionAttr minPrecision;
  UnitAttr nonUniform;
};

} // namespace

static OptionalParseResult tryParseIndexList(AsmParser &parser, SMLoc fieldLoc,
                                             OperandIndexAttr &result) {
  if (failed(parser.parseOptionalLSquare()))
    return std::nullopt;
  if (result)
    return parser.emitError(fieldLoc) << "duplicate index list";

  SmallVector<IndexAttr> entries;
  if (failed(parser.parseOptionalRSquare())) {
    if (parser.parseCommaSeparatedList(
            AsmParser::Delimiter::None,
            [&]() -> ParseResult {
              IndexAttr entry;
              if (parser.parseCustomAttributeWithFallback(entry))
                return failure();
              entries.push_back(entry);
              return success();
            }) ||
        parser.parseRSquare())
      return failure();
  }
  result = OperandIndexAttr::get(parser.getContext(), entries);
  return success();
}

static OptionalParseResult parseSrcOperandBody(AsmParser &parser,
                                               SrcOperandBody &body);

static FailureOr<SrcOperandAttr> parseRelativeSrcOperand(AsmParser &parser,
                                                         OperandType type) {
  auto *ctx = parser.getContext();
  SrcOperandBody body;
  OptionalParseResult bodyResult = parseSrcOperandBody(parser, body);
  if (bodyResult.has_value() && failed(*bodyResult))
    return failure();
  // The builder fills in canonical components and the identity swizzle.
  return SrcOperandAttr::get(ctx, type, body.index, body.swizzle,
                             body.components, body.minPrecision,
                             OperandModifierAttr(), body.nonUniform);
}

static ParseResult setSingleIndexEntry(AsmParser &parser, MLIRContext *ctx,
                                       SMLoc fieldLoc, OperandIndexAttr &index,
                                       IndexAttr entry) {
  if (index)
    return parser.emitError(fieldLoc) << "duplicate index list";
  index = OperandIndexAttr::get(ctx, ArrayRef<IndexAttr>{entry});
  return success();
}

/// Parses a single bare index entry (`5`, `5 : i64`, `5 + r<...>`), absent when
/// the next token is not an integer.
static OptionalParseResult tryParseImmIndex(AsmParser &parser, MLIRContext *ctx,
                                            SMLoc fieldLoc,
                                            OperandIndexAttr &index) {
  auto loc = parser.getCurrentLocation();
  uint64_t value = 0;
  OptionalParseResult intResult = parser.parseOptionalInteger(value);
  if (!intResult.has_value())
    return std::nullopt;
  if (failed(*intResult))
    return failure();

  auto entry = parseImmIndexEntryTail(parser, ctx, loc, value);
  if (!entry)
    return failure();
  return setSingleIndexEntry(parser, ctx, fieldLoc, index, entry);
}

static ParseResult parseOperandBody(AsmParser &parser,
                                    function_ref<ParseResult()> parseField) {
  if (succeeded(parser.parseOptionalGreater()))
    return success();
  return failure(
      parser.parseCommaSeparatedList(AsmParser::Delimiter::None, parseField) ||
      parser.parseGreater());
}

/// Handles a keyword that names an operand type used as a relative index
/// (`r<...>`), absent when `keyword` is not an operand type.
static OptionalParseResult
keywordToRelativeIndex(AsmParser &parser, MLIRContext *ctx, SMLoc fieldLoc,
                       StringRef keyword, OperandIndexAttr &index) {
  auto type = symbolizeOperandType(keyword);
  if (!type)
    return std::nullopt;
  if (isImmediateType(*type))
    return parser.emitError(fieldLoc)
           << "immediate operand `" << keyword << "` cannot be an index";
  auto relative = parseRelativeSrcOperand(parser, *type);
  if (failed(relative))
    return failure();
  return setSingleIndexEntry(parser, ctx, fieldLoc, index,
                             IndexAttr::get(ctx, IntegerAttr(), *relative));
}

static OptionalParseResult
keywordToComponents(AsmParser &parser, MLIRContext *ctx, SMLoc fieldLoc,
                    StringRef keyword, OperandComponentsAttr &components) {
  auto parsed = symbolizeOperandComponents(keyword);
  if (!parsed)
    return std::nullopt;
  if (components)
    return parser.emitError(fieldLoc) << "duplicate component count";
  components = OperandComponentsAttr::get(ctx, *parsed);
  return success();
}

static OptionalParseResult
keywordToMinPrecision(AsmParser &parser, MLIRContext *ctx, SMLoc fieldLoc,
                      StringRef keyword,
                      OperandMinPrecisionAttr &minPrecision) {
  auto parsed = symbolizeOperandMinPrecision(keyword);
  if (!parsed)
    return std::nullopt;
  if (minPrecision)
    return parser.emitError(fieldLoc) << "duplicate min precision";
  minPrecision = OperandMinPrecisionAttr::get(ctx, *parsed);
  return success();
}

/// Parses the keyword-led operand-body fields shared by source and destination
/// operands. `bodyKind` ("source" / "destination") only shapes the diagnostic.
static ParseResult
parseKeywordOperandField(AsmParser &parser, MLIRContext *ctx, SMLoc fieldLoc,
                         StringRef bodyKind, OperandIndexAttr &index,
                         OperandComponentsAttr &components,
                         OperandMinPrecisionAttr &minPrecision) {
  StringRef keyword;
  if (parser.parseKeyword(&keyword))
    return failure();
  if (auto r = keywordToRelativeIndex(parser, ctx, fieldLoc, keyword, index);
      r.has_value())
    return *r;
  if (auto r = keywordToComponents(parser, ctx, fieldLoc, keyword, components);
      r.has_value())
    return *r;
  if (auto r =
          keywordToMinPrecision(parser, ctx, fieldLoc, keyword, minPrecision);
      r.has_value())
    return *r;
  return parser.emitError(fieldLoc) << "unexpected keyword in " << bodyKind
                                    << " operand body: `" << keyword << "`";
}

//===----------------------------------------------------------------------===//
// DstOperandAttr
//===----------------------------------------------------------------------===//

/// Builds a destination writemask from a parsed component list.
static FailureOr<ComponentMaskAttr>
buildComponentMask(function_ref<InFlightDiagnostic()> emitError,
                   MLIRContext *ctx, ArrayRef<unsigned> components) {
  if (components.empty() || components.size() > 4)
    return emitError() << "destination mask must have 1 to 4 components, got "
                       << components.size();
  auto bits = static_cast<ComponentMask>(0);
  for (unsigned c : components) {
    auto bit = static_cast<ComponentMask>(1u << c);
    if ((bits & bit) == bit)
      return emitError() << "duplicate component `" << charFromComponent(c)
                         << "` in destination mask";
    bits = bits | bit;
  }
  return ComponentMaskAttr::get(ctx, bits);
}

/// Parses a destination writemask `<x, y, ...>`, absent when the next token is
/// not `<`.
static OptionalParseResult tryParseMask(AsmParser &parser, MLIRContext *ctx,
                                        SMLoc fieldLoc,
                                        ComponentMaskAttr &mask) {
  SmallVector<unsigned, 4> components;
  OptionalParseResult componentList =
      parseOptionalComponents(parser, components);
  if (!componentList.has_value())
    return std::nullopt;
  if (failed(*componentList))
    return failure();
  if (mask)
    return parser.emitError(fieldLoc) << "duplicate mask";
  FailureOr<ComponentMaskAttr> built = buildComponentMask(
      [&] { return parser.emitError(fieldLoc); }, ctx, components);
  if (failed(built))
    return failure();
  mask = *built;
  return success();
}

static OptionalParseResult parseDstOperandBody(AsmParser &parser,
                                               DstOperandBody &body) {
  if (failed(parser.parseOptionalLess()))
    return std::nullopt;
  auto *ctx = parser.getContext();
  return parseOperandBody(parser, [&]() -> ParseResult {
    SMLoc fieldLoc = parser.getCurrentLocation();
    if (auto r = tryParseIndexList(parser, fieldLoc, body.index); r.has_value())
      return *r;
    if (auto r = tryParseImmIndex(parser, ctx, fieldLoc, body.index);
        r.has_value())
      return *r;
    if (auto r = tryParseMask(parser, ctx, fieldLoc, body.mask); r.has_value())
      return *r;
    return parseKeywordOperandField(parser, ctx, fieldLoc, "destination",
                                    body.index, body.components,
                                    body.minPrecision);
  });
}

static OperandComponentsAttr
nonDefaultComponents(OperandComponentsAttr components, OperandType type) {
  if (components && components.getValue() == defaultComponentsFor(type))
    return {};
  return components;
}

static ComponentMaskAttr nonDefaultMask(ComponentMaskAttr mask) {
  if (mask && bitEnumContainsAll(mask.getValue(),
                                 ComponentMask::x | ComponentMask::y |
                                     ComponentMask::z | ComponentMask::w))
    return {};
  return mask;
}

static SwizzleAttr nonDefaultSwizzle(SwizzleAttr swizzle) {
  if (swizzle && swizzle.getComponents() == ArrayRef<unsigned>{0, 1, 2, 3})
    return {};
  return swizzle;
}

static void printOperandIndex(AsmPrinter &printer, OperandIndexAttr index) {
  bool useBrackets = index.size() != 1;
  if (useBrackets)
    printer << '[';
  llvm::interleaveComma(index, printer.getStream(), [&](IndexAttr entry) {
    printer.printStrippedAttrOrType(entry);
  });
  if (useBrackets)
    printer << ']';
}

static void printOperandComponents(AsmPrinter &printer,
                                   OperandComponentsAttr components) {
  printer << stringifyOperandComponents(components.getValue());
}

static void printOperandMinPrecision(AsmPrinter &printer,
                                     OperandMinPrecisionAttr minPrecision) {
  printer << stringifyOperandMinPrecision(minPrecision.getValue());
}

static void printOperandMask(AsmPrinter &printer, ComponentMaskAttr mask) {
  printer.printStrippedAttrOrType(mask);
}

static void printOperandBody(AsmPrinter &printer,
                             ArrayRef<std::function<void()>> fields) {
  if (fields.empty())
    return;
  printer << '<';
  llvm::interleaveComma(fields, printer.getStream(),
                        [](const std::function<void()> &field) { field(); });
  printer << '>';
}

LogicalResult DstOperandAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, OperandType /*type*/,
    OperandIndexAttr /*index*/, OperandComponentsAttr components,
    OperandMinPrecisionAttr /*minPrecision*/, ComponentMaskAttr /*mask*/) {
  if (!components)
    return emitError() << "component count is required";
  return success();
}

Attribute DstOperandAttr::parse(AsmParser &parser, Type) {
  auto loc = parser.getCurrentLocation();
  auto *ctx = parser.getContext();

  StringRef typeKeyword;
  if (parser.parseKeyword(&typeKeyword))
    return {};
  auto operandType = symbolizeOperandType(typeKeyword);
  if (!operandType) {
    parser.emitError(loc) << "unknown operand type: `" << typeKeyword << "`";
    return {};
  }
  if (isImmediateType(*operandType)) {
    parser.emitError(loc) << "immediate operand `" << typeKeyword
                          << "` cannot be a destination";
    return {};
  }

  DstOperandBody body;
  OptionalParseResult bodyResult = parseDstOperandBody(parser, body);
  if (bodyResult.has_value() && failed(*bodyResult))
    return {};

  return parser.getChecked<DstOperandAttr>(loc, ctx, *operandType, body.index,
                                           body.mask, body.components,
                                           body.minPrecision);
}

void DstOperandAttr::print(AsmPrinter &printer) const {
  auto index = getIndex();
  auto minPrecision = getMinPrecision();
  auto components = nonDefaultComponents(getComponents(), getType());
  auto mask = nonDefaultMask(getMask());

  printer << stringifyOperandType(getType());

  SmallVector<std::function<void()>, 4> fields;
  if (index)
    fields.push_back([&] { printOperandIndex(printer, index); });
  if (components)
    fields.push_back([&] { printOperandComponents(printer, components); });
  if (minPrecision)
    fields.push_back([&] { printOperandMinPrecision(printer, minPrecision); });
  if (mask)
    fields.push_back([&] { printOperandMask(printer, mask); });

  printOperandBody(printer, fields);
}

//===----------------------------------------------------------------------===//
// SrcOperandAttr custom asm
//===----------------------------------------------------------------------===//

static Attribute parseImmSrcOperand(AsmParser &parser, MLIRContext *ctx,
                                    SMLoc loc, OperandType type,
                                    OperandModifierAttr modifier) {
  DenseI32ArrayAttr values32;
  DenseI64ArrayAttr values64;
  switch (type) {
  case OperandType::l:
    if (parseImm32Body(parser, values32))
      return {};
    break;
  case OperandType::d:
    if (parseImm64Body(parser, values64))
      return {};
    break;
  default:
    llvm_unreachable("non-immediate operand type in immediate parser");
  }
  // The component count is derived from the payload by the builder; an invalid
  // element count is reported by the verifier.
  return parser.getChecked<SrcOperandAttr>(loc, ctx, values32, values64,
                                           modifier);
}

static Attribute
parseNegAndAbsModifier(AsmParser &parser, MLIRContext *ctx,
                       function_ref<Attribute(OperandModifierAttr)> parseBody) {
  bool hasMinus = succeeded(parser.parseOptionalMinus());
  bool hasAbs = succeeded(parser.parseOptionalVerticalBar());

  OperandModifierAttr modifier;
  if (hasMinus && hasAbs)
    modifier = OperandModifierAttr::get(ctx, OperandModifier::abs_neg);
  else if (hasMinus)
    modifier = OperandModifierAttr::get(ctx, OperandModifier::neg);
  else if (hasAbs)
    modifier = OperandModifierAttr::get(ctx, OperandModifier::abs);

  Attribute result = parseBody(modifier);
  if (!result)
    return {};
  if (hasAbs && parser.parseVerticalBar())
    return {};
  return result;
}

static void printNegAndAbsModifier(AsmPrinter &printer,
                                   OperandModifierAttr modifier,
                                   function_ref<void()> printBody) {
  bool isNeg = modifier && (modifier.getValue() == OperandModifier::neg ||
                            modifier.getValue() == OperandModifier::abs_neg);
  bool isAbs = modifier && (modifier.getValue() == OperandModifier::abs ||
                            modifier.getValue() == OperandModifier::abs_neg);
  if (isNeg)
    printer << '-';
  if (isAbs)
    printer << '|';

  printBody();

  if (isAbs)
    printer << '|';
}

/// Parses the source component selection `<...>`, absent when the next token
/// is not `<`. The list size (1 or 4) distinguishes a single-component select
/// from a full swizzle.
static OptionalParseResult tryParseSwizzle(AsmParser &parser, MLIRContext *ctx,
                                           SMLoc fieldLoc,
                                           SwizzleAttr &swizzle) {
  SmallVector<unsigned, 4> components;
  OptionalParseResult componentList =
      parseOptionalComponents(parser, components);
  if (!componentList.has_value())
    return std::nullopt;
  if (failed(*componentList))
    return failure();
  auto parsed = parser.getChecked<SwizzleAttr>(fieldLoc, ctx, components);
  if (!parsed)
    return failure();
  if (swizzle)
    return parser.emitError(fieldLoc) << "duplicate swizzle";
  swizzle = parsed;
  return success();
}

/// Parses the bare `nonuniform` keyword, absent when it is not the next token.
static OptionalParseResult tryParseNonUniform(AsmParser &parser,
                                              MLIRContext *ctx, SMLoc fieldLoc,
                                              UnitAttr &nonUniform) {
  if (failed(parser.parseOptionalKeyword(nonUniformKeyword)))
    return std::nullopt;
  if (nonUniform)
    return parser.emitError(fieldLoc) << "duplicate " << nonUniformKeyword;
  nonUniform = UnitAttr::get(ctx);
  return success();
}

static OptionalParseResult parseSrcOperandBody(AsmParser &parser,
                                               SrcOperandBody &body) {
  if (failed(parser.parseOptionalLess()))
    return std::nullopt;
  auto *ctx = parser.getContext();
  return parseOperandBody(parser, [&]() -> ParseResult {
    SMLoc fieldLoc = parser.getCurrentLocation();
    if (auto r = tryParseIndexList(parser, fieldLoc, body.index); r.has_value())
      return *r;
    if (auto r = tryParseImmIndex(parser, ctx, fieldLoc, body.index);
        r.has_value())
      return *r;
    if (auto r = tryParseSwizzle(parser, ctx, fieldLoc, body.swizzle);
        r.has_value())
      return *r;
    if (auto r = tryParseNonUniform(parser, ctx, fieldLoc, body.nonUniform);
        r.has_value())
      return *r;
    return parseKeywordOperandField(parser, ctx, fieldLoc, "source", body.index,
                                    body.components, body.minPrecision);
  });
}

static void printSrcOperandBody(AsmPrinter &printer, SrcOperandAttr attr) {
  auto index = attr.getIndex();
  auto minPrecision = attr.getMinPrecision();
  auto nonUniform = attr.getNonUniform();

  auto components = nonDefaultComponents(attr.getComponents(), attr.getType());
  auto swizzle = nonDefaultSwizzle(attr.getSwizzle());

  printer << stringifyOperandType(attr.getType());

  SmallVector<std::function<void()>, 6> fields;
  if (index)
    fields.push_back([&] { printOperandIndex(printer, index); });
  if (components)
    fields.push_back([&] { printOperandComponents(printer, components); });
  if (minPrecision)
    fields.push_back([&] { printOperandMinPrecision(printer, minPrecision); });
  if (nonUniform)
    fields.push_back([&] { printer << nonUniformKeyword; });
  if (swizzle)
    fields.push_back([&] { printer.printStrippedAttrOrType(swizzle); });

  printOperandBody(printer, fields);
}

LogicalResult SrcOperandAttr::verify(
    function_ref<InFlightDiagnostic()> emitError, OperandType type,
    OperandIndexAttr /*index*/, OperandComponentsAttr components,
    OperandMinPrecisionAttr /*minPrecision*/, UnitAttr /*nonUniform*/,
    SwizzleAttr /*swizzle*/, OperandModifierAttr modifier,
    DenseI32ArrayAttr values32, DenseI64ArrayAttr values64) {
  bool hasValues32 = static_cast<bool>(values32);
  bool hasValues64 = static_cast<bool>(values64);

  if (!components)
    return emitError() << "component count is required";
  if (hasValues32 && hasValues64)
    return emitError() << "values32 and values64 are mutually exclusive";
  if (hasValues32 && type != OperandType::l)
    return emitError() << "values32 is only valid for operand type `l`, got `"
                       << stringifyOperandType(type) << "`";
  if (hasValues64 && type != OperandType::d)
    return emitError() << "values64 is only valid for operand type `d`, got `"
                       << stringifyOperandType(type) << "`";
  if (type == OperandType::l && !hasValues32)
    return emitError() << "operand type `l` requires a values32 literal";
  if (type == OperandType::d && !hasValues64)
    return emitError() << "operand type `d` requires a values64 literal";
  if (isImmediateType(type) && modifier)
    return emitError() << "immediate operands cannot have a source modifier";
  if (isImmediateType(type)) {
    auto count = hasValues32 ? values32.size() : values64.size();
    auto expected = immComponentsFor(type, count);
    if (!expected)
      return emitError() << "type `" << stringifyOperandType(type)
                         << "` immediate has an invalid element count: "
                         << count;
    if (components.getValue() != *expected)
      return emitError() << "component count `"
                         << stringifyOperandComponents(components.getValue())
                         << "` does not match the immediate payload";
  }
  return success();
}

Attribute SrcOperandAttr::parse(AsmParser &parser, Type) {
  auto loc = parser.getCurrentLocation();
  auto *ctx = parser.getContext();

  return parseNegAndAbsModifier(
      parser, ctx, [&](OperandModifierAttr modifier) -> Attribute {
        auto typeLoc = parser.getCurrentLocation();
        StringRef typeKeyword;
        if (parser.parseKeyword(&typeKeyword))
          return {};
        auto type = symbolizeOperandType(typeKeyword);
        if (!type) {
          parser.emitError(typeLoc)
              << "unknown operand type: `" << typeKeyword << "`";
          return {};
        }

        if (isImmediateType(*type))
          return parseImmSrcOperand(parser, ctx, loc, *type, modifier);

        SrcOperandBody body;
        OptionalParseResult bodyResult = parseSrcOperandBody(parser, body);
        if (bodyResult.has_value() && failed(*bodyResult))
          return {};

        // The builder fills in canonical components and the identity swizzle.
        return parser.getChecked<SrcOperandAttr>(
            loc, ctx, *type, body.index, body.swizzle, body.components,
            body.minPrecision, modifier, body.nonUniform);
      });
}

void SrcOperandAttr::print(AsmPrinter &printer) const {
  if (auto values32 = getValues32())
    printImm32(printer, values32.asArrayRef());
  else if (auto values64 = getValues64())
    printImm64(printer, values64.asArrayRef());
  else
    printNegAndAbsModifier(printer, getModifier(),
                           [&] { printSrcOperandBody(printer, *this); });
}

LogicalResult
SampleOffsetAttr::verify(function_ref<InFlightDiagnostic()> emitError,
                         int32_t u, int32_t v, int32_t w) {
  int32_t values[] = {u, v, w};
  for (int32_t value : values) {
    if (value < -8 || value > 7) {
      return emitError()
             << "sample offsets must be 4 bit 2's complement numbers, "
                "having integer range [-8,7], got "
             << value;
    }
  }
  return success();
}
