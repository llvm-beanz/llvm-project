//===- SPIRVImporterTest.cpp - Tests for feme::SPIRVImporter -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "feme/Import/SPIRV/SPIRVImporter.h"

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"

#include <cstring>
#include <string>
#include <vector>

using namespace feme;

namespace {

// Note: the "imports a valid SPIR-V binary into a `spirv.module`" case is
// deliberately not covered here. It requires building a real serialized
// SPIR-V binary, which is exactly the kind of binary-format round trip
// `feme-translate` exists to exercise via `lit`/`FileCheck` instead (see
// "Testing Strategy" in feme/docs/Design.md); see
// `test/Import/SPIRV/spirv-import.mlir`.

TEST(SPIRVImporterTest, GetFormatName) {
  SPIRVImporter Importer;
  EXPECT_EQ(Importer.getFormatName(), "spirv");
}

TEST(SPIRVImporterTest, RejectsNonWordAlignedInput) {
  Context Ctx;
  SPIRVImporter Importer;
  // 3 bytes: not a multiple of 4, so cannot be a stream of SPIR-V words.
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef("abc", "spirv-test"), ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

TEST(SPIRVImporterTest, RejectsMalformedBinary) {
  Context Ctx;
  SPIRVImporter Importer;
  // 4 bytes of garbage: word-aligned, but not a valid SPIR-V module (wrong
  // magic number), so deserialization itself must fail.
  llvm::Expected<Module> Result =
      Importer.import(llvm::MemoryBufferRef(
                          llvm::StringRef("\xde\xad\xbe\xef", 4), "spirv-test"),
                      ImportOptions{}, Ctx);
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

/// A minimal SPIR-V hand-assembler for the one test below that needs an
/// `OpExtInst` referencing an extended-instruction-set name MLIR's own
/// `spirv` dialect has no representation for at all (`NonSemantic.*`, per
/// the SPIR-V specification's own "instruction sets with no semantic
/// effect" convention) -- unlike every other importer test in this file,
/// this cannot be produced by `feme-translate`'s serialize/import round
/// trip (see `test/Import/SPIRV/spirv-import.mlir`'s own comment), since
/// there is no `spirv` dialect op to author it with in the first place.
class RawSPIRVModuleBuilder {
public:
  RawSPIRVModuleBuilder() {
    // The 5-word SPIR-V module header: magic number, version 1.0, a
    // generator magic number (unused by this ICD), an initial <id> bound
    // (raised as new <id>s are allocated below), and a reserved schema
    // word.
    Words = {0x07230203, 0x00010000, 0, 1, 0};
  }

  uint32_t nextId() { return Bound++; }

  void emit(uint32_t Opcode, llvm::ArrayRef<uint32_t> Operands) {
    Words.push_back(((static_cast<uint32_t>(Operands.size()) + 1) << 16) |
                    Opcode);
    Words.append(Operands.begin(), Operands.end());
  }

  /// Encodes \p Str as a SPIR-V "Literal String" (a null-terminated,
  /// zero-padded run of 32-bit words, each holding up to 4 UTF-8 bytes
  /// little-endian).
  static std::vector<uint32_t> literalString(llvm::StringRef Str) {
    std::string Bytes = Str.str();
    Bytes.push_back('\0');
    while (Bytes.size() % 4 != 0)
      Bytes.push_back('\0');
    std::vector<uint32_t> Result;
    for (size_t I = 0; I != Bytes.size(); I += 4) {
      uint32_t Word;
      std::memcpy(&Word, Bytes.data() + I, 4);
      Result.push_back(Word);
    }
    return Result;
  }

  std::vector<uint32_t> finish() {
    Words[3] = Bound;
    return std::vector<uint32_t>(Words.begin(), Words.end());
  }

private:
  llvm::SmallVector<uint32_t> Words;
  uint32_t Bound = 1;
};

/// A minimal `void main()` module whose one instruction is an `OpExtInst`
/// call into a `NonSemantic.DebugPrintf`-named extended instruction set
/// (`VK_KHR_shader_non_semantic_info`, roadmap E19) -- exactly the shape
/// `SPIRVImporter.cpp`'s `stripNonSemanticExtInst` must strip before MLIR's
/// deserializer (which has no case for this set name at all) ever sees it.
std::vector<uint32_t> buildNonSemanticExtInstModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t ExtSet = B.nextId();
  uint32_t ExtResult = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  {
    std::vector<uint32_t> Operands{ExtSet};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString(
                                     "NonSemantic.DebugPrintf"));
    B.emit(/*OpExtInstImport=*/11, Operands);
  }
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*Vertex=*/0, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  // `%ExtResult = OpExtInst %Void %ExtSet 1` -- instruction number 1 of
  // `NonSemantic.DebugPrintf`, with no operands of its own.
  B.emit(/*OpExtInst=*/12, {Void, ExtResult, ExtSet, /*Instruction=*/1});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

TEST(SPIRVImporterTest, StripsNonSemanticExtInst) {
  Context Ctx;
  SPIRVImporter Importer;
  std::vector<uint32_t> Words = buildNonSemanticExtInstModule();
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef(
          llvm::StringRef(reinterpret_cast<const char *>(Words.data()),
                          Words.size() * sizeof(uint32_t)),
          "spirv-test"),
      ImportOptions{}, Ctx);
  // Without stripping, this fails: MLIR's deserializer has no case for the
  // `NonSemantic.DebugPrintf` extended-instruction-set name at all (see
  // `stripNonSemanticExtInst`'s own comment).
  EXPECT_THAT_EXPECTED(Result, llvm::Succeeded());
}

/// Encodes \p F as its raw IEEE-754 bit pattern -- the "Literal
/// ContextDependentNumber" encoding `OpConstant` uses for a 32-bit
/// floating-point value.
uint32_t floatBits(float F) {
  uint32_t Bits;
  std::memcpy(&Bits, &F, sizeof(Bits));
  return Bits;
}

llvm::Expected<Module> importModule(Context &Ctx,
                                    const std::vector<uint32_t> &Words) {
  SPIRVImporter Importer;
  return Importer.import(
      llvm::MemoryBufferRef(
          llvm::StringRef(reinterpret_cast<const char *>(Words.data()),
                          Words.size() * sizeof(uint32_t)),
          "spirv-test"),
      ImportOptions{}, Ctx);
}

/// A minimal fragment-shader-shaped module sampling a plain 2D image via
/// `OpImageSampleProjExplicitLod` (92) -- the opcode
/// `lowerProjectiveImageSamples` (see `SPIRVImporter.cpp`) must rewrite
/// into `OpImageSampleExplicitLod` (88) before MLIR's deserializer, which
/// has no enum case for 92 at all (roadmap L72(a)), ever sees it. The
/// Coordinate operand is a `vec3` (u, v, q) built via `OpConstantComposite`
/// -- a producer that pass's own opcode allowlist recognizes -- so its
/// exact shape is resolvable; deliberately no `vec2` type is declared
/// anywhere else in this module, exercising the pass's own
/// synthesized-vector-type path.
std::vector<uint32_t> buildProjExplicitLodModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t Float = B.nextId();
  uint32_t Vec3 = B.nextId();
  uint32_t Vec4 = B.nextId();
  uint32_t ImageTy = B.nextId();
  uint32_t SampledImageTy = B.nextId();
  uint32_t PtrSampledImage = B.nextId();
  uint32_t Variable = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t U = B.nextId();
  uint32_t V = B.nextId();
  uint32_t Q = B.nextId();
  uint32_t Coord = B.nextId();
  uint32_t Lod = B.nextId();
  uint32_t SampledImageVal = B.nextId();
  uint32_t Result = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*Fragment=*/4, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpExecutionMode=*/16, {Main, /*OriginUpperLeft=*/7});
  B.emit(/*OpDecorate=*/71, {Variable, /*DescriptorSet=*/34, 0});
  B.emit(/*OpDecorate=*/71, {Variable, /*Binding=*/33, 0});
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeFloat=*/22, {Float, 32});
  B.emit(/*OpTypeVector=*/23, {Vec3, Float, 3});
  B.emit(/*OpTypeVector=*/23, {Vec4, Float, 4});
  B.emit(/*OpTypeImage=*/25, {ImageTy, Float, /*Dim2D=*/1, /*Depth=*/0,
                              /*Arrayed=*/0, /*MS=*/0, /*Sampled=*/1,
                              /*Unknown=*/0});
  B.emit(/*OpTypeSampledImage=*/27, {SampledImageTy, ImageTy});
  B.emit(/*OpTypePointer=*/32,
         {PtrSampledImage, /*UniformConstant=*/0, SampledImageTy});
  B.emit(/*OpVariable=*/59, {PtrSampledImage, Variable, /*UniformConstant=*/0});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpConstant=*/43, {Float, U, floatBits(1.0f)});
  B.emit(/*OpConstant=*/43, {Float, V, floatBits(2.0f)});
  B.emit(/*OpConstant=*/43, {Float, Q, floatBits(2.0f)});
  B.emit(/*OpConstantComposite=*/44, {Vec3, Coord, U, V, Q});
  B.emit(/*OpConstant=*/43, {Float, Lod, floatBits(0.0f)});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  B.emit(/*OpLoad=*/61, {SampledImageTy, SampledImageVal, Variable});
  // `%Result = OpImageSampleProjExplicitLod %Vec4 %SampledImageVal %Coord
  //   Lod %Lod`.
  B.emit(/*OpImageSampleProjExplicitLod=*/92,
         {Vec4, Result, SampledImageVal, Coord, /*Lod=*/2, Lod});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

/// As `buildProjExplicitLodModule` above, but a depth-comparison
/// (`OpImageSampleProjDrefExplicitLod`, 94) sample instead, exercising
/// `lowerProjectiveImageSamples`'s own Dref-divide path.
std::vector<uint32_t> buildProjDrefExplicitLodModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t Float = B.nextId();
  uint32_t Vec3 = B.nextId();
  uint32_t ImageTy = B.nextId();
  uint32_t SampledImageTy = B.nextId();
  uint32_t PtrSampledImage = B.nextId();
  uint32_t Variable = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t U = B.nextId();
  uint32_t V = B.nextId();
  uint32_t Q = B.nextId();
  uint32_t Coord = B.nextId();
  uint32_t Dref = B.nextId();
  uint32_t Lod = B.nextId();
  uint32_t SampledImageVal = B.nextId();
  uint32_t Result = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*Fragment=*/4, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpExecutionMode=*/16, {Main, /*OriginUpperLeft=*/7});
  B.emit(/*OpDecorate=*/71, {Variable, /*DescriptorSet=*/34, 0});
  B.emit(/*OpDecorate=*/71, {Variable, /*Binding=*/33, 0});
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeFloat=*/22, {Float, 32});
  B.emit(/*OpTypeVector=*/23, {Vec3, Float, 3});
  B.emit(/*OpTypeImage=*/25, {ImageTy, Float, /*Dim2D=*/1, /*Depth=*/0,
                              /*Arrayed=*/0, /*MS=*/0, /*Sampled=*/1,
                              /*Unknown=*/0});
  B.emit(/*OpTypeSampledImage=*/27, {SampledImageTy, ImageTy});
  B.emit(/*OpTypePointer=*/32,
         {PtrSampledImage, /*UniformConstant=*/0, SampledImageTy});
  B.emit(/*OpVariable=*/59, {PtrSampledImage, Variable, /*UniformConstant=*/0});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpConstant=*/43, {Float, U, floatBits(1.0f)});
  B.emit(/*OpConstant=*/43, {Float, V, floatBits(2.0f)});
  B.emit(/*OpConstant=*/43, {Float, Q, floatBits(2.0f)});
  B.emit(/*OpConstantComposite=*/44, {Vec3, Coord, U, V, Q});
  B.emit(/*OpConstant=*/43, {Float, Dref, floatBits(0.5f)});
  B.emit(/*OpConstant=*/43, {Float, Lod, floatBits(0.0f)});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  B.emit(/*OpLoad=*/61, {SampledImageTy, SampledImageVal, Variable});
  // `%Result = OpImageSampleProjDrefExplicitLod %Float %SampledImageVal
  //   %Coord %Dref Lod %Lod`.
  B.emit(/*OpImageSampleProjDrefExplicitLod=*/94,
         {Float, Result, SampledImageVal, Coord, Dref, /*Lod=*/2, Lod});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

/// As `buildProjExplicitLodModule`, but the Coordinate operand is instead
/// produced by `OpIAdd` -- an opcode `lowerProjectiveImageSamples`'s own
/// producer allowlist deliberately does not recognize -- so this exercises
/// that pass's "leave the instruction alone rather than risk an incorrect
/// divide" fallback.
std::vector<uint32_t> buildProjExplicitLodWithUnresolvableCoordinateModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t Float = B.nextId();
  uint32_t Vec4 = B.nextId();
  uint32_t Int = B.nextId();
  uint32_t ImageTy = B.nextId();
  uint32_t SampledImageTy = B.nextId();
  uint32_t PtrSampledImage = B.nextId();
  uint32_t Variable = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t A = B.nextId();
  uint32_t C = B.nextId();
  uint32_t Coord = B.nextId();
  uint32_t Lod = B.nextId();
  uint32_t SampledImageVal = B.nextId();
  uint32_t Result = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*Fragment=*/4, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpExecutionMode=*/16, {Main, /*OriginUpperLeft=*/7});
  B.emit(/*OpDecorate=*/71, {Variable, /*DescriptorSet=*/34, 0});
  B.emit(/*OpDecorate=*/71, {Variable, /*Binding=*/33, 0});
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeFloat=*/22, {Float, 32});
  B.emit(/*OpTypeVector=*/23, {Vec4, Float, 4});
  B.emit(/*OpTypeInt=*/21, {Int, 32, /*Signed=*/1});
  B.emit(/*OpTypeImage=*/25, {ImageTy, Float, /*Dim2D=*/1, /*Depth=*/0,
                              /*Arrayed=*/0, /*MS=*/0, /*Sampled=*/1,
                              /*Unknown=*/0});
  B.emit(/*OpTypeSampledImage=*/27, {SampledImageTy, ImageTy});
  B.emit(/*OpTypePointer=*/32,
         {PtrSampledImage, /*UniformConstant=*/0, SampledImageTy});
  B.emit(/*OpVariable=*/59, {PtrSampledImage, Variable, /*UniformConstant=*/0});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpConstant=*/43, {Int, A, 1});
  B.emit(/*OpConstant=*/43, {Int, C, 2});
  B.emit(/*OpConstant=*/43, {Float, Lod, floatBits(0.0f)});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  B.emit(/*OpLoad=*/61, {SampledImageTy, SampledImageVal, Variable});
  B.emit(/*OpIAdd=*/128, {Int, Coord, A, C});
  B.emit(/*OpImageSampleProjExplicitLod=*/92,
         {Vec4, Result, SampledImageVal, Coord, /*Lod=*/2, Lod});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

TEST(SPIRVImporterTest, LowersImageSampleProjExplicitLod) {
  Context Ctx;
  std::vector<uint32_t> Words = buildProjExplicitLodModule();
  llvm::Expected<Module> Result = importModule(Ctx, Words);
  // Without `lowerProjectiveImageSamples`, this fails: MLIR's deserializer
  // has no enum case for `OpImageSampleProjExplicitLod` (92) at all (see
  // roadmap L72(a)).
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  unsigned SampleCount = 0, FDivCount = 0, ConstructCount = 0;
  Result->getMLIROperation()->walk(
      [&](mlir::spirv::ImageSampleExplicitLodOp Op) {
        ++SampleCount;
        // The rewritten Coordinate must be narrowed to exactly 2
        // components (this image is 2D), not left at its original
        // 3-component projective width.
        auto CoordTy =
            llvm::cast<mlir::VectorType>(Op.getCoordinate().getType());
        EXPECT_EQ(CoordTy.getNumElements(), 2);
      });
  Result->getMLIROperation()->walk([&](mlir::spirv::FDivOp) { ++FDivCount; });
  Result->getMLIROperation()->walk(
      [&](mlir::spirv::CompositeConstructOp) { ++ConstructCount; });
  EXPECT_EQ(SampleCount, 1u);
  // One divide per real coordinate component: u/q, v/q.
  EXPECT_EQ(FDivCount, 2u);
  // The two divided components are reassembled into the narrowed vec2.
  EXPECT_EQ(ConstructCount, 1u);
}

TEST(SPIRVImporterTest, LowersImageSampleProjDrefExplicitLod) {
  Context Ctx;
  std::vector<uint32_t> Words = buildProjDrefExplicitLodModule();
  llvm::Expected<Module> Result = importModule(Ctx, Words);
  // Without `lowerProjectiveImageSamples`, this fails: MLIR's deserializer
  // has no enum case for `OpImageSampleProjDrefExplicitLod` (94) at all
  // (see roadmap L72(a)).
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  unsigned SampleCount = 0, FDivCount = 0;
  Result->getMLIROperation()->walk(
      [&](mlir::spirv::ImageSampleDrefExplicitLodOp Op) {
        ++SampleCount;
        auto CoordTy =
            llvm::cast<mlir::VectorType>(Op.getCoordinate().getType());
        EXPECT_EQ(CoordTy.getNumElements(), 2);
      });
  Result->getMLIROperation()->walk([&](mlir::spirv::FDivOp) { ++FDivCount; });
  EXPECT_EQ(SampleCount, 1u);
  // u/q, v/q, and dref/q.
  EXPECT_EQ(FDivCount, 3u);
}

TEST(SPIRVImporterTest,
     LeavesImageSampleProjExplicitLodWithUnresolvableCoordinateAlone) {
  Context Ctx;
  std::vector<uint32_t> Words =
      buildProjExplicitLodWithUnresolvableCoordinateModule();
  llvm::Expected<Module> Result = importModule(Ctx, Words);
  // `lowerProjectiveImageSamples` cannot resolve an `OpIAdd`-produced
  // Coordinate's vector shape (deliberately not in its own producer
  // allowlist) and must leave the instruction untouched rather than risk
  // an incorrect divide -- so this fails exactly as it did before that
  // pass existed (MLIR still has no enum case for opcode 92).
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

/// A minimal compute-shader-shaped module querying a plain 2D storage
/// image's size at an explicit mip level via `OpImageQuerySizeLod` (103)
/// -- the opcode `lowerImageQueryOpcodes` (see `SPIRVImporter.cpp`) must
/// rewrite into an `OpFunctionCall` before MLIR's deserializer, which has
/// no enum case for 103 at all (roadmap L72(d)), ever sees it.
std::vector<uint32_t> buildImageQuerySizeLodModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t Int = B.nextId();
  uint32_t IVec2 = B.nextId();
  uint32_t ImageTy = B.nextId();
  uint32_t PtrImage = B.nextId();
  uint32_t Variable = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t Lod = B.nextId();
  uint32_t ImageVal = B.nextId();
  uint32_t Result = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*GLCompute=*/5, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpExecutionMode=*/16, {Main, /*LocalSize=*/17, 1, 1, 1});
  B.emit(/*OpDecorate=*/71, {Variable, /*DescriptorSet=*/34, 0});
  B.emit(/*OpDecorate=*/71, {Variable, /*Binding=*/33, 0});
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeInt=*/21, {Int, 32, /*Signed=*/1});
  B.emit(/*OpTypeVector=*/23, {IVec2, Int, 2});
  B.emit(/*OpTypeImage=*/25, {ImageTy, Int, /*Dim2D=*/1, /*Depth=*/0,
                              /*Arrayed=*/0, /*MS=*/0, /*Sampled=*/2,
                              /*Rgba32i=*/24});
  B.emit(/*OpTypePointer=*/32, {PtrImage, /*UniformConstant=*/0, ImageTy});
  B.emit(/*OpVariable=*/59, {PtrImage, Variable, /*UniformConstant=*/0});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpConstant=*/43, {Int, Lod, 0});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  B.emit(/*OpLoad=*/61, {ImageTy, ImageVal, Variable});
  // `%Result = OpImageQuerySizeLod %IVec2 %ImageVal %Lod`.
  B.emit(/*OpImageQuerySizeLod=*/103, {IVec2, Result, ImageVal, Lod});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

/// As `buildImageQuerySizeLodModule`, but queries the image's own
/// mip-level count via `OpImageQueryLevels` (106) instead, and the Image
/// operand is extracted from a combined sampled image via `OpImage` --
/// the shape a `sampler2D`-typed GLSL query builtin actually produces --
/// exercising `isKnownResultTypeProducer`'s `OpImage` case.
std::vector<uint32_t> buildImageQueryLevelsModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t Int = B.nextId();
  uint32_t Float = B.nextId();
  uint32_t ImageTy = B.nextId();
  uint32_t SampledImageTy = B.nextId();
  uint32_t PtrSampledImage = B.nextId();
  uint32_t Variable = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t SampledImageVal = B.nextId();
  uint32_t ImageVal = B.nextId();
  uint32_t Result = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*Fragment=*/4, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpExecutionMode=*/16, {Main, /*OriginUpperLeft=*/7});
  B.emit(/*OpDecorate=*/71, {Variable, /*DescriptorSet=*/34, 0});
  B.emit(/*OpDecorate=*/71, {Variable, /*Binding=*/33, 0});
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeInt=*/21, {Int, 32, /*Signed=*/1});
  B.emit(/*OpTypeFloat=*/22, {Float, 32});
  B.emit(/*OpTypeImage=*/25, {ImageTy, Float, /*Dim2D=*/1, /*Depth=*/0,
                              /*Arrayed=*/0, /*MS=*/0, /*Sampled=*/1,
                              /*Unknown=*/0});
  B.emit(/*OpTypeSampledImage=*/27, {SampledImageTy, ImageTy});
  B.emit(/*OpTypePointer=*/32,
         {PtrSampledImage, /*UniformConstant=*/0, SampledImageTy});
  B.emit(/*OpVariable=*/59, {PtrSampledImage, Variable, /*UniformConstant=*/0});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  B.emit(/*OpLoad=*/61, {SampledImageTy, SampledImageVal, Variable});
  B.emit(/*OpImage=*/100, {ImageTy, ImageVal, SampledImageVal});
  // `%Result = OpImageQueryLevels %Int %ImageVal`.
  B.emit(/*OpImageQueryLevels=*/106, {Int, Result, ImageVal});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

/// As `buildImageQuerySizeLodModule`, but the Lod operand is instead
/// produced by `OpIAdd` -- an opcode `isKnownResultTypeProducer`
/// deliberately does not recognize -- so this exercises
/// `lowerImageQueryOpcodes`'s "leave the instruction alone rather than
/// risk an incorrectly-typed call" fallback.
std::vector<uint32_t> buildImageQuerySizeLodWithUnresolvableLodModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t Int = B.nextId();
  uint32_t IVec2 = B.nextId();
  uint32_t ImageTy = B.nextId();
  uint32_t PtrImage = B.nextId();
  uint32_t Variable = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t A = B.nextId();
  uint32_t C = B.nextId();
  uint32_t Lod = B.nextId();
  uint32_t ImageVal = B.nextId();
  uint32_t Result = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*GLCompute=*/5, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpExecutionMode=*/16, {Main, /*LocalSize=*/17, 1, 1, 1});
  B.emit(/*OpDecorate=*/71, {Variable, /*DescriptorSet=*/34, 0});
  B.emit(/*OpDecorate=*/71, {Variable, /*Binding=*/33, 0});
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeInt=*/21, {Int, 32, /*Signed=*/1});
  B.emit(/*OpTypeVector=*/23, {IVec2, Int, 2});
  B.emit(/*OpTypeImage=*/25, {ImageTy, Int, /*Dim2D=*/1, /*Depth=*/0,
                              /*Arrayed=*/0, /*MS=*/0, /*Sampled=*/2,
                              /*Rgba32i=*/24});
  B.emit(/*OpTypePointer=*/32, {PtrImage, /*UniformConstant=*/0, ImageTy});
  B.emit(/*OpVariable=*/59, {PtrImage, Variable, /*UniformConstant=*/0});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpConstant=*/43, {Int, A, 1});
  B.emit(/*OpConstant=*/43, {Int, C, 2});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  B.emit(/*OpLoad=*/61, {ImageTy, ImageVal, Variable});
  B.emit(/*OpIAdd=*/128, {Int, Lod, A, C});
  B.emit(/*OpImageQuerySizeLod=*/103, {IVec2, Result, ImageVal, Lod});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

TEST(SPIRVImporterTest, LowersImageQuerySizeLod) {
  Context Ctx;
  std::vector<uint32_t> Words = buildImageQuerySizeLodModule();
  llvm::Expected<Module> Result = importModule(Ctx, Words);
  // Without `lowerImageQueryOpcodes`, this fails: MLIR's deserializer has
  // no enum case for `OpImageQuerySizeLod` (103) at all (see roadmap
  // L72(d)).
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  unsigned CallCount = 0;
  Result->getMLIROperation()->walk([&](mlir::spirv::FunctionCallOp Op) {
    ++CallCount;
    EXPECT_TRUE(
        llvm::StringRef(Op.getCallee()).starts_with("feme.query.size_lod."));
    ASSERT_EQ(Op.getArguments().size(), 2u);
    // Result type is the queried vec2<i32> size; the two arguments are the
    // plain image handle and the explicit Level-of-Detail.
    EXPECT_TRUE(llvm::isa<mlir::VectorType>(Op.getType(0)));
  });
  EXPECT_EQ(CallCount, 1u);

  // The synthesized callee itself must be an external (no-body), `Import`
  // linkage function declaration -- otherwise MLIR's own module verifier
  // would have rejected the module outright.
  unsigned ExternalFuncCount = 0;
  Result->getMLIROperation()->walk([&](mlir::spirv::FuncOp Op) {
    if (llvm::StringRef(Op.getName()).starts_with("feme.query.size_lod.")) {
      ++ExternalFuncCount;
      EXPECT_TRUE(Op.isExternal());
    }
  });
  EXPECT_EQ(ExternalFuncCount, 1u);
}

TEST(SPIRVImporterTest, LowersImageQueryLevels) {
  Context Ctx;
  std::vector<uint32_t> Words = buildImageQueryLevelsModule();
  llvm::Expected<Module> Result = importModule(Ctx, Words);
  // Without `lowerImageQueryOpcodes`, this fails: MLIR's deserializer has
  // no enum case for `OpImageQueryLevels` (106) at all (see roadmap
  // L72(d)).
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  unsigned CallCount = 0;
  Result->getMLIROperation()->walk([&](mlir::spirv::FunctionCallOp Op) {
    ++CallCount;
    EXPECT_TRUE(
        llvm::StringRef(Op.getCallee()).starts_with("feme.query.levels."));
    // A single argument: the plain image handle (`OpImageQueryLevels` has
    // no Level-of-Detail operand of its own -- it queries the image's own
    // total mip-level count).
    ASSERT_EQ(Op.getArguments().size(), 1u);
    EXPECT_TRUE(llvm::isa<mlir::IntegerType>(Op.getType(0)));
  });
  EXPECT_EQ(CallCount, 1u);
}

TEST(SPIRVImporterTest, LeavesImageQuerySizeLodWithUnresolvableLodAlone) {
  Context Ctx;
  std::vector<uint32_t> Words =
      buildImageQuerySizeLodWithUnresolvableLodModule();
  llvm::Expected<Module> Result = importModule(Ctx, Words);
  // `lowerImageQueryOpcodes` cannot resolve an `OpIAdd`-produced Lod
  // operand's type (deliberately not in `isKnownResultTypeProducer`'s own
  // allowlist) and must leave the instruction untouched rather than risk
  // an incorrectly-typed call -- so this fails exactly as it did before
  // that pass existed (MLIR still has no enum case for opcode 103).
  EXPECT_THAT_EXPECTED(Result, llvm::Failed());
}

/// As `buildImageQueryLevelsModule`, but queries a multisampled sampled
/// image's own sample count via `OpImageQuerySamples` (107) instead
/// (roadmap L73, GLSL's `textureSamples(sampler2DMS)`) -- the image type
/// itself is declared `MS=1` (multisampled), the shape a real
/// `sampler2DMS` produces.
std::vector<uint32_t> buildImageQuerySamplesModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t Int = B.nextId();
  uint32_t Float = B.nextId();
  uint32_t ImageTy = B.nextId();
  uint32_t SampledImageTy = B.nextId();
  uint32_t PtrSampledImage = B.nextId();
  uint32_t Variable = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t SampledImageVal = B.nextId();
  uint32_t ImageVal = B.nextId();
  uint32_t Result = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*Fragment=*/4, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  B.emit(/*OpExecutionMode=*/16, {Main, /*OriginUpperLeft=*/7});
  B.emit(/*OpDecorate=*/71, {Variable, /*DescriptorSet=*/34, 0});
  B.emit(/*OpDecorate=*/71, {Variable, /*Binding=*/33, 0});
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeInt=*/21, {Int, 32, /*Signed=*/1});
  B.emit(/*OpTypeFloat=*/22, {Float, 32});
  B.emit(/*OpTypeImage=*/25, {ImageTy, Float, /*Dim2D=*/1, /*Depth=*/0,
                              /*Arrayed=*/0, /*MS=*/1, /*Sampled=*/1,
                              /*Unknown=*/0});
  B.emit(/*OpTypeSampledImage=*/27, {SampledImageTy, ImageTy});
  B.emit(/*OpTypePointer=*/32,
         {PtrSampledImage, /*UniformConstant=*/0, SampledImageTy});
  B.emit(/*OpVariable=*/59, {PtrSampledImage, Variable, /*UniformConstant=*/0});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  B.emit(/*OpLoad=*/61, {SampledImageTy, SampledImageVal, Variable});
  B.emit(/*OpImage=*/100, {ImageTy, ImageVal, SampledImageVal});
  // `%Result = OpImageQuerySamples %Int %ImageVal`.
  B.emit(/*OpImageQuerySamples=*/107, {Int, Result, ImageVal});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

TEST(SPIRVImporterTest, LowersImageQuerySamples) {
  Context Ctx;
  std::vector<uint32_t> Words = buildImageQuerySamplesModule();
  llvm::Expected<Module> Result = importModule(Ctx, Words);
  // Without `lowerImageQueryOpcodes`, this fails: MLIR's deserializer has
  // no enum case for `OpImageQuerySamples` (107) at all (see roadmap L73).
  ASSERT_THAT_EXPECTED(Result, llvm::Succeeded());

  unsigned CallCount = 0;
  Result->getMLIROperation()->walk([&](mlir::spirv::FunctionCallOp Op) {
    ++CallCount;
    EXPECT_TRUE(
        llvm::StringRef(Op.getCallee()).starts_with("feme.query.samples."));
    // A single argument: the plain image handle (`OpImageQuerySamples` has
    // no Level-of-Detail operand of its own -- it queries the image's own
    // multisample sample count).
    ASSERT_EQ(Op.getArguments().size(), 1u);
    EXPECT_TRUE(llvm::isa<mlir::IntegerType>(Op.getType(0)));
  });
  EXPECT_EQ(CallCount, 1u);
}

/// A minimal `void main()` module with two distinct `OpTypeStruct` <id>s,
/// each named "Z" via its own `OpName` (but with different member lists,
/// so the two are not merely duplicate declarations of an identical
/// type) -- exactly the shape a sufficiently deeply-nested HLSL
/// `ConstantBuffer` struct compiles to (see `Feature/ConstantBufferT/
/// nested.test`, bug https://github.com/llvm/llvm-project/issues/180600):
/// DXC must emit one Uniform-layout copy and one StorageBuffer-layout copy
/// of the same source-level struct once it can no longer flatten the
/// struct into its enclosing cbuffer wrapper, and both copies keep the
/// same plain source-level debug name. MLIR's deserializer resolves an
/// `OpName`-carrying `OpTypeStruct` to an "identified" `spirv::StructType`
/// keyed purely by that name string (see `processStructType` in
/// `mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp`), so without
/// `disambiguateDuplicateStructNames`'s rewrite, the second struct's
/// `trySetBody` call collides with the first's and fails -- silently, with
/// no diagnostic at all -- rather than merely being treated as two
/// distinct types.
std::vector<uint32_t> buildDuplicateNamedStructModule() {
  RawSPIRVModuleBuilder B;
  uint32_t Void = B.nextId();
  uint32_t FnTy = B.nextId();
  uint32_t Main = B.nextId();
  uint32_t Label = B.nextId();
  uint32_t I32 = B.nextId();
  uint32_t StructA = B.nextId();
  uint32_t StructB = B.nextId();

  B.emit(/*OpCapability=*/17, {/*Shader=*/1});
  B.emit(/*OpMemoryModel=*/14, {/*Logical=*/0, /*GLSL450=*/1});
  {
    std::vector<uint32_t> Operands{/*Vertex=*/0, Main};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("main"));
    B.emit(/*OpEntryPoint=*/15, Operands);
  }
  {
    std::vector<uint32_t> Operands{StructA};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("Z"));
    B.emit(/*OpName=*/5, Operands);
  }
  {
    std::vector<uint32_t> Operands{StructB};
    llvm::append_range(Operands, RawSPIRVModuleBuilder::literalString("Z"));
    B.emit(/*OpName=*/5, Operands);
  }
  B.emit(/*OpTypeInt=*/21, {I32, 32, /*Signed=*/1});
  // `%StructA = OpTypeStruct %I32` -- one `i32` member.
  B.emit(/*OpTypeStruct=*/30, {StructA, I32});
  // `%StructB = OpTypeStruct %I32 %I32` -- two `i32` members: a genuinely
  // different body from `%StructA`'s, despite sharing its debug name.
  B.emit(/*OpTypeStruct=*/30, {StructB, I32, I32});
  B.emit(/*OpTypeVoid=*/19, {Void});
  B.emit(/*OpTypeFunction=*/33, {FnTy, Void});
  B.emit(/*OpFunction=*/54, {Void, Main, /*None=*/0, FnTy});
  B.emit(/*OpLabel=*/248, {Label});
  B.emit(/*OpReturn=*/253, {});
  B.emit(/*OpFunctionEnd=*/56, {});
  return B.finish();
}

TEST(SPIRVImporterTest, DisambiguatesDuplicateStructNames) {
  Context Ctx;
  SPIRVImporter Importer;
  std::vector<uint32_t> Words = buildDuplicateNamedStructModule();
  llvm::Expected<Module> Result = Importer.import(
      llvm::MemoryBufferRef(
          llvm::StringRef(reinterpret_cast<const char *>(Words.data()),
                          Words.size() * sizeof(uint32_t)),
          "spirv-test"),
      ImportOptions{}, Ctx);
  // Without `disambiguateDuplicateStructNames`, this fails: MLIR's
  // deserializer resolves both `OpTypeStruct`s to the same identified
  // `spirv::StructType` (keyed by the shared "Z" name) and the second
  // `trySetBody` call -- since the member lists genuinely differ --
  // fails, silently, with no diagnostic (see this test's own comment).
  EXPECT_THAT_EXPECTED(Result, llvm::Succeeded());
}

} // namespace
