//===- SignatureComments.cpp - fxc signature table reader ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements parseSignatureComments. `fxc` prints each signature as a
// fixed-width table introduced by a "<kind> signature:" line:
//
//   // Input signature:
//   //
//   // Name                 Index   Mask Register SysValue  Format   Used
//   // -------------------- ----- ------ -------- -------- ------- ------
//   // A                        0   xyzw        0     NONE   float    yz
//   //
//
// The columns are wide enough for most names but not all of them, so the
// rows are split on whitespace rather than by column position: a name never
// contains a space, which makes the seven fields unambiguous (the trailing
// "Used" field may be blank).
//
//===----------------------------------------------------------------------===//

#include "feme/DXBC/Assembler/SignatureComments.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"

using namespace feme::dxbc;
using namespace llvm;

namespace {

/// Which of a shader's three signatures a table describes.
enum class TableKind { None, Input, Output, PatchConstant };

/// The `SysValue` column, whose abbreviations are `fxc`'s own spelling of
/// `D3D_NAME`.
std::optional<dxbc::D3DSystemValue> parseSystemValue(StringRef Text) {
  return StringSwitch<std::optional<dxbc::D3DSystemValue>>(Text)
      .Case("NONE", dxbc::D3DSystemValue::Undefined)
      .Case("POS", dxbc::D3DSystemValue::Position)
      .Case("CLIPDST", dxbc::D3DSystemValue::ClipDistance)
      .Case("CULLDST", dxbc::D3DSystemValue::CullDistance)
      .Case("RTINDEX", dxbc::D3DSystemValue::RenderTargetArrayIndex)
      .Case("VPINDEX", dxbc::D3DSystemValue::ViewPortArrayIndex)
      .Case("VERTID", dxbc::D3DSystemValue::VertexID)
      .Case("PRIMID", dxbc::D3DSystemValue::PrimitiveID)
      .Case("INSTID", dxbc::D3DSystemValue::InstanceID)
      .Case("FFACE", dxbc::D3DSystemValue::IsFrontFace)
      .Case("SAMPLE", dxbc::D3DSystemValue::SampleIndex)
      .Case("QUADEDGE", dxbc::D3DSystemValue::FinalQuadEdgeTessfactor)
      .Case("QUADINT", dxbc::D3DSystemValue::FinalQuadInsideTessfactor)
      .Case("TRIEDGE", dxbc::D3DSystemValue::FinalTriEdgeTessfactor)
      .Case("TRIINT", dxbc::D3DSystemValue::FinalTriInsideTessfactor)
      .Case("LINEDET", dxbc::D3DSystemValue::FinalLineDetailTessfactor)
      .Case("LINEDEN", dxbc::D3DSystemValue::FinalLineDensityTessfactor)
      .Case("BARYCENTRICS", dxbc::D3DSystemValue::Barycentrics)
      .Case("SHADINGRATE", dxbc::D3DSystemValue::ShadingRate)
      .Case("CULLPRIM", dxbc::D3DSystemValue::CullPrimitive)
      .Case("TARGET", dxbc::D3DSystemValue::Target)
      .Case("DEPTH", dxbc::D3DSystemValue::Depth)
      .Case("COVERAGE", dxbc::D3DSystemValue::Coverage)
      .Case("DEPTHGE", dxbc::D3DSystemValue::DepthGE)
      .Case("DEPTHLE", dxbc::D3DSystemValue::DepthLE)
      .Case("STENCILREF", dxbc::D3DSystemValue::StencilRef)
      .Case("INNERCOV", dxbc::D3DSystemValue::InnerCoverage)
      .Default(std::nullopt);
}

/// The `Format` column. The legacy signature parts predate minimum
/// precision, which `fxc` records in the newer `ISG1`/`OSG1` parts and in
/// the operand tokens themselves, so a `min16*` element is a 32-bit one
/// here.
std::optional<dxbc::SigComponentType> parseComponentType(StringRef Text) {
  return StringSwitch<std::optional<dxbc::SigComponentType>>(Text)
      .Cases({"float", "min16f", "min10f"}, dxbc::SigComponentType::Float32)
      .Cases({"uint", "min16u"}, dxbc::SigComponentType::UInt32)
      .Cases({"int", "min16i", "min12i"}, dxbc::SigComponentType::SInt32)
      .Case("double", dxbc::SigComponentType::Float64)
      .Default(std::nullopt);
}

/// A `Mask`/`Used` column: some subset of "xyzw", or a placeholder `fxc`
/// prints for an element that occupies no register. The column is printed
/// with the components in their fixed positions, so an entry with a gap in
/// it arrives here as several whitespace-separated pieces.
std::optional<uint8_t> parseComponentMask(ArrayRef<StringRef> Fields) {
  uint8_t Mask = 0;
  for (StringRef Text : Fields) {
    if (Text == "N/A" || Text == "YES" || Text == "NO")
      continue;
    for (char C : Text) {
      size_t Component = StringRef("xyzw").find(C);
      if (Component == StringRef::npos)
        return std::nullopt;
      Mask |= uint8_t(1u << Component);
    }
  }
  return Mask;
}

/// Recognizes the line introducing a signature table.
TableKind tableIntroducedBy(StringRef Line) {
  if (!Line.consume_back("signature:"))
    return TableKind::None;
  Line = Line.trim();
  return StringSwitch<TableKind>(Line)
      .Case("Input", TableKind::Input)
      .Case("Output", TableKind::Output)
      .Case("Patch Constant", TableKind::PatchConstant)
      .Default(TableKind::None);
}

/// Parses one table row, or returns nullopt when \p Fields is not one --
/// which is how the end of a table is recognized.
///
/// The columns are fixed-width and a name may overflow its own, so the row
/// is anchored on the `SysValue`/`Format` pair: those two are single words
/// drawn from disjoint, known vocabularies, which fixes where the variable
/// number of whitespace-separated pieces a component mask can be printed
/// as ("x z") begins and ends.
std::optional<SignatureElement> parseRow(ArrayRef<StringRef> Fields,
                                         bool Input) {
  size_t Format = 0;
  for (size_t I = Fields.size(); I-- > 5;)
    if (parseComponentType(Fields[I]) && parseSystemValue(Fields[I - 1])) {
      Format = I;
      break;
    }
  if (!Format)
    return std::nullopt;

  SignatureElement Element;
  // A geometry shader's output rows are prefixed with the stream they
  // belong to, which the legacy layout has no field for.
  StringRef Name = Fields[0];
  if (size_t Colon = Name.find(':');
      Colon != StringRef::npos && Name.starts_with("m")) {
    unsigned Stream = 0;
    if (to_integer(Name.substr(1, Colon - 1), Stream, 10))
      Name = Name.substr(Colon + 1);
  }
  Element.Name = Name.str();

  if (!to_integer(Fields[1], Element.Index, 10))
    return std::nullopt;
  std::optional<uint8_t> Mask = parseComponentMask(Fields.slice(2, Format - 4));
  if (!Mask)
    return std::nullopt;
  Element.Mask = *Mask;
  Element.SystemValue = *parseSystemValue(Fields[Format - 1]);
  Element.CompType = *parseComponentType(Fields[Format]);

  // A registerless element names the output operand instead of a number,
  // and occupies the single component that operand stands for.
  if (!to_integer(Fields[Format - 2], Element.Register, 10)) {
    Element.Register = SignatureElement::NoRegister;
    Element.Mask = 1;
  }

  std::optional<uint8_t> Used =
      parseComponentMask(Fields.drop_front(Format + 1));
  if (!Used)
    return std::nullopt;
  // `ExclusiveMask` means "always read" in an input signature and "never
  // written" in an output one, where the table's `Used` column always
  // means "read or written".
  Element.ExclusiveMask = Input ? *Used : uint8_t(Element.Mask & ~*Used);
  return Element;
}

} // namespace

Signatures feme::dxbc::parseSignatureComments(StringRef Source) {
  Signatures Result;
  TableKind Kind = TableKind::None;

  SmallVector<StringRef, 64> Lines;
  Source.split(Lines, '\n');
  for (StringRef Line : Lines) {
    Line = Line.rtrim("\r \t");
    StringRef Body = Line.ltrim();
    if (!Body.consume_front("//")) {
      // The tables only ever appear in the comment banner above the
      // instruction stream.
      Kind = TableKind::None;
      continue;
    }
    Body = Body.trim();

    if (TableKind Introduced = tableIntroducedBy(Body);
        Introduced != TableKind::None) {
      Kind = Introduced;
      switch (Kind) {
      case TableKind::Input:
        Result.SeenInput = true;
        break;
      case TableKind::Output:
        Result.SeenOutput = true;
        break;
      case TableKind::PatchConstant:
        Result.SeenPatchConstant = true;
        break;
      case TableKind::None:
        break;
      }
      continue;
    }
    if (Kind == TableKind::None || Body.empty() || Body.starts_with("Name ") ||
        Body.starts_with("---"))
      continue;

    SmallVector<StringRef, 8> Fields;
    for (StringRef Rest = Body; !Rest.empty();) {
      Rest = Rest.ltrim();
      if (Rest.empty())
        break;
      size_t End = Rest.find_first_of(" \t");
      Fields.push_back(Rest.substr(0, End));
      Rest = End == StringRef::npos ? StringRef() : Rest.substr(End);
    }
    std::optional<SignatureElement> Element =
        parseRow(Fields, Kind == TableKind::Input);
    if (!Element) {
      // "no Input" and anything else unrecognized ends the table.
      Kind = TableKind::None;
      continue;
    }
    switch (Kind) {
    case TableKind::Input:
      Result.Input.push_back(std::move(*Element));
      break;
    case TableKind::Output:
      Result.Output.push_back(std::move(*Element));
      break;
    case TableKind::PatchConstant:
      Result.PatchConstant.push_back(std::move(*Element));
      break;
    case TableKind::None:
      break;
    }
  }
  return Result;
}
