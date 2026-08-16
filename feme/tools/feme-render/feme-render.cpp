//===- feme-render.cpp - FeMe software graphics executor runner ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// feme-render is the tool described in "Testing Tools" in
// feme/docs/Design.md and docs/CommandGuide/feme-render.md: it renders a
// textual scene description (feme::graphics::parseScene) through FeMe's
// software graphics executor and prints the resulting attachments as
// textual image fixtures (feme::graphics::printImageFixture) -- the
// graphics counterpart of feme-run.
//
// Roadmap R31 ("FeMeGraphics skeleton", see feme/docs/Roadmap.md) is a
// skeleton milestone: it implements the normalized pipeline/prepared-draw
// *descriptions* (feme/include/feme/Graphics/{Pipeline,PreparedDraw}.h),
// not the executor that walks them. This tool therefore:
//
//   - always builds and clears every `attachments` entry;
//   - compiles `pipeline.vertex`/`pipeline.fragment` into a real
//     `feme::graphics::GraphicsPipeline` when the scene has a `pipeline`
//     key, proving the normalized-pipeline description builds end to end
//     from a real scene file and real compiled stages;
//   - diagnoses a non-empty `draws` list as not yet implemented, rather
//     than silently rendering nothing or the wrong thing -- draw execution
//     (vertex/index fetch, clipping, rasterization, interpolation) is
//     roadmap R32, "Basic triangle pipeline";
//   - dumps attachments (`--dump`, default every color attachment) and
//     supports `--expect`/`--tolerance` comparison against a checked-in
//     image fixture.
//
// Shader modules are loaded as plain, already-raised LLVM IR (`.ll`/`.bc`)
// only for now, the same convention feme-run's own milestone-4 scope used
// before DXIL/SPIR-V import support was added to it -- this tool grows the
// same support once a test needs it, following the same pattern.
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Graphics/ImageFixture.h"
#include "feme/Graphics/Pipeline.h"
#include "feme/Graphics/Scene.h"
#include "feme/Target/CPU/CompiledStage.h"
#include "feme/Target/CPU/Pipeline.h"
#include "feme/Transforms/DXIL/SignatureImport.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace llvm;
using namespace feme;
using namespace feme::graphics;

namespace {

/// Loads \p Path as already-raised LLVM IR (see the file comment above).
/// A hand-authored shader that does not itself use any `feme.stage.*`
/// input/output operation carries no `feme.signature` metadata
/// (`feme::dxil::setEntrySignature`, see feme/lib/Transforms/DXIL/
/// SignatureImport.cpp) the way a real DXIL/SPIR-V import would attach --
/// the vertex/fragment wrappers require it regardless (`feme-cpu-wrap-
/// vertex`/`feme-cpu-wrap-fragment`), so this attaches an empty one to
/// \p EntryPoint if it has none, matching what an import would produce for
/// a shader with no signature elements.
Expected<feme::Module> loadShaderModule(StringRef Path, StringRef EntryPoint,
                                        feme::Context &Ctx) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(Path);
  if (std::error_code EC = BufOrErr.getError())
    return createStringError(EC, "could not open '%s': %s", Path.str().c_str(),
                             EC.message().c_str());

  SMDiagnostic Err;
  std::unique_ptr<llvm::Module> LLVMMod =
      parseIR((*BufOrErr)->getMemBufferRef(), Err, Ctx.getLLVMContext());
  if (!LLVMMod) {
    std::string Message;
    raw_string_ostream OS(Message);
    Err.print("feme-render", OS);
    return createStringError(inconvertibleErrorCode(), "%s", Message.c_str());
  }

  if (llvm::Function *Entry =
          LLVMMod->getFunction(EntryPoint.empty() ? "main" : EntryPoint))
    if (!Entry->getMetadata(feme::dxil::getEntrySignatureMDKind()))
      feme::dxil::setEntrySignature(*Entry, feme::EntrySignature{});

  return feme::Module::fromLLVMIR(std::move(LLVMMod));
}

Expected<CullMode> parseCullMode(StringRef Cull) {
  if (Cull == "none")
    return CullMode::None;
  if (Cull == "front")
    return CullMode::Front;
  if (Cull == "back")
    return CullMode::Back;
  return createStringError(inconvertibleErrorCode(),
                           "unknown pipeline 'cull: %s'", Cull.str().c_str());
}

Expected<FrontFace> parseFrontFace(StringRef Front) {
  if (Front == "ccw")
    return FrontFace::CounterClockwise;
  if (Front == "cw")
    return FrontFace::Clockwise;
  return createStringError(inconvertibleErrorCode(),
                           "unknown pipeline 'front-face: %s'",
                           Front.str().c_str());
}

Expected<BlendMode> parseBlendMode(StringRef Blend) {
  if (Blend == "replace")
    return BlendMode::Replace;
  return createStringError(inconvertibleErrorCode(),
                           "pipeline 'blend: %s' is not yet supported "
                           "(roadmap R33, 'Depth, stencil, blending, and "
                           "multisampling')",
                           Blend.str().c_str());
}

Expected<CompareOp> parseCompareOp(StringRef Compare) {
  std::optional<CompareOp> Result =
      StringSwitch<std::optional<CompareOp>>(Compare)
          .Case("never", CompareOp::Never)
          .Case("less", CompareOp::Less)
          .Case("equal", CompareOp::Equal)
          .Case("lessequal", CompareOp::LessEqual)
          .Case("greater", CompareOp::Greater)
          .Case("notequal", CompareOp::NotEqual)
          .Case("greaterequal", CompareOp::GreaterEqual)
          .Case("always", CompareOp::Always)
          .Default(std::nullopt);
  if (!Result)
    return createStringError(inconvertibleErrorCode(),
                             "unknown pipeline 'depth.test: %s'",
                             Compare.str().c_str());
  return *Result;
}

Expected<DepthState> buildDepthState(const SceneDepthState &Depth) {
  DepthState State;
  State.WriteEnable = Depth.Write;
  if (Depth.Test == "none") {
    State.TestEnable = false;
    return State;
  }
  Expected<CompareOp> Compare = parseCompareOp(Depth.Test);
  if (!Compare)
    return Compare.takeError();
  State.TestEnable = true;
  State.Compare = *Compare;
  return State;
}

/// Compiles \p Stage's module as \p Kind (vertex or fragment), matching
/// feme-run's own JIT-compile convention (`feme::cpu::StageCompileOptions`,
/// see feme/docs/FeMeGraphicsDesign.md's "CPU Lowering Pipeline"). A
/// relative `module` path is resolved against \p SceneDir (the scene
/// file's own directory), matching how the scene's `textures` file paths
/// are documented to resolve (see "Textual scene and image fixtures" in
/// feme/docs/Design.md: shader modules are "referenced by path").
Expected<std::shared_ptr<cpu::CompiledStage>>
compileStage(feme::Context &Ctx, StringRef SceneDir,
            const SceneShaderStage &Stage, feme::ShaderStage Kind,
            unsigned WaveSize, CodeGenOptLevel OptLevel) {
  SmallString<128> ModulePath(Stage.Module);
  if (!SceneDir.empty() && !sys::path::is_absolute(ModulePath)) {
    ModulePath = SceneDir;
    sys::path::append(ModulePath, Stage.Module);
  }

  Expected<feme::Module> Mod = loadShaderModule(ModulePath, Stage.Entry, Ctx);
  if (!Mod)
    return Mod.takeError();

  cpu::StageCompileOptions Opts;
  Opts.Stage = Kind;
  Opts.EntryPoint = Stage.Entry;
  Opts.WaveSize = WaveSize;
  Opts.OptLevel = OptLevel;

  Expected<std::unique_ptr<cpu::CompiledStage>> Compiled =
      cpu::CompiledStage::create(Ctx, std::move(*Mod), Opts);
  if (!Compiled)
    return Compiled.takeError();
  return std::shared_ptr<cpu::CompiledStage>(std::move(*Compiled));
}

/// Compiles \p P's vertex/fragment modules and builds the normalized
/// `GraphicsPipeline` description from them plus \p Attachments (see
/// feme/include/feme/Graphics/Pipeline.h).
Expected<GraphicsPipeline>
buildPipeline(feme::Context &Ctx, StringRef SceneDir, const ScenePipeline &P,
             unsigned WaveSize,
             CodeGenOptLevel OptLevel,
             std::vector<AttachmentFormat> Attachments) {
  Expected<std::shared_ptr<cpu::CompiledStage>> Vertex =
      compileStage(Ctx, SceneDir, P.Vertex, feme::ShaderStage::Vertex,
                  WaveSize, OptLevel);
  if (!Vertex)
    return Vertex.takeError();
  Expected<std::shared_ptr<cpu::CompiledStage>> Fragment =
      compileStage(Ctx, SceneDir, P.Fragment, feme::ShaderStage::Fragment,
                  WaveSize, OptLevel);
  if (!Fragment)
    return Fragment.takeError();
  Expected<CullMode> Cull = parseCullMode(P.Cull);
  if (!Cull)
    return Cull.takeError();
  Expected<FrontFace> Front = parseFrontFace(P.FrontFace);
  if (!Front)
    return Front.takeError();
  Expected<BlendMode> Blend = parseBlendMode(P.Blend);
  if (!Blend)
    return Blend.takeError();
  Expected<DepthState> Depth = buildDepthState(P.Depth);
  if (!Depth)
    return Depth.takeError();

  return GraphicsPipeline(std::move(*Vertex), std::move(*Fragment),
                          PrimitiveTopology::TriangleList,
                          RasterState{*Cull, *Front}, *Depth, *Blend,
                          /*SampleCount=*/1, std::move(Attachments));
}

/// One built attachment: its identity plus owned backing storage.
struct AttachmentStorage {
  std::string Name;
  cpu::ResourceFormat Format = cpu::ResourceFormat::Unknown;
  uint32_t Width = 0;
  uint32_t Height = 0;
  std::vector<uint8_t> Data;
};

/// Builds and clears every scene `attachments` entry.
Expected<std::vector<AttachmentStorage>>
buildAttachments(ArrayRef<SceneAttachment> Attachments) {
  std::vector<AttachmentStorage> Result;
  Result.reserve(Attachments.size());
  for (const SceneAttachment &A : Attachments) {
    Expected<cpu::ResourceFormat> Format = parseFixtureFormat(A.Format);
    if (!Format)
      return Format.takeError();
    Expected<uint32_t> ElemSize = getFixtureFormatElementSize(*Format);
    if (!ElemSize)
      return ElemSize.takeError();

    AttachmentStorage Storage;
    Storage.Name = A.Name;
    Storage.Format = *Format;
    Storage.Width = A.Width;
    Storage.Height = A.Height;
    Storage.Data.assign((size_t)A.Width * A.Height * *ElemSize, 0);

    if (!A.Clear.empty()) {
      std::vector<uint8_t> Texel(*ElemSize);
      if (Error E = packClearColor(*Format, A.Clear, Texel))
        return std::move(E);
      for (size_t I = 0; I != (size_t)A.Width * A.Height; ++I)
        memcpy(Storage.Data.data() + I * *ElemSize, Texel.data(), *ElemSize);
    }

    Result.push_back(std::move(Storage));
  }
  return Result;
}

/// Prints \p Attachment as a textual image fixture.
Error dumpAttachment(raw_ostream &OS, const AttachmentStorage &Attachment) {
  ImageFixture Fixture;
  Fixture.Name = Attachment.Name;
  Fixture.Format = Attachment.Format;
  Fixture.Width = Attachment.Width;
  Fixture.Height = Attachment.Height;
  Fixture.Data = Attachment.Data;
  return printImageFixture(OS, Fixture);
}

/// Compares \p Attachment's data against \p Expected's, exactly if
/// \p Tolerance is 0, or per-component (for a floating-point format only --
/// see `isFixtureFormatFloat`'s own comment) within \p Tolerance otherwise.
Error compareAttachment(const AttachmentStorage &Attachment,
                        const ImageFixture &ExpectedImage, double Tolerance) {
  if (Attachment.Width != ExpectedImage.Width ||
      Attachment.Height != ExpectedImage.Height ||
      Attachment.Format != ExpectedImage.Format)
    return createStringError(inconvertibleErrorCode(),
                             "'%s' does not match --expect's extent/format",
                             Attachment.Name.c_str());

  if (Tolerance == 0.0) {
    if (Attachment.Data != ExpectedImage.Data)
      return createStringError(inconvertibleErrorCode(),
                               "'%s' does not match --expect",
                               Attachment.Name.c_str());
    return Error::success();
  }

  Expected<bool> IsFloat = isFixtureFormatFloat(Attachment.Format);
  if (!IsFloat)
    return IsFloat.takeError();
  if (!*IsFloat)
    return createStringError(inconvertibleErrorCode(),
                             "'--tolerance' is not yet supported for this "
                             "attachment's format");

  if (Attachment.Data.size() != ExpectedImage.Data.size() ||
      Attachment.Data.size() % sizeof(float) != 0)
    return createStringError(inconvertibleErrorCode(),
                             "'%s' does not match --expect",
                             Attachment.Name.c_str());
  for (size_t I = 0; I != Attachment.Data.size(); I += sizeof(float)) {
    float Actual, Expect;
    memcpy(&Actual, Attachment.Data.data() + I, sizeof(float));
    memcpy(&Expect, ExpectedImage.Data.data() + I, sizeof(float));
    if (std::fabs(Actual - Expect) > Tolerance)
      return createStringError(inconvertibleErrorCode(),
                               "'%s' does not match --expect within "
                               "--tolerance=%f",
                               Attachment.Name.c_str(), Tolerance);
  }
  return Error::success();
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  cl::opt<std::string> SceneFilename(cl::Positional, cl::Required,
                                     cl::desc("<scene .yaml file>"));
  cl::opt<unsigned> WaveSize(
      "wave-size", cl::init(0),
      cl::desc("The wave size to compile every stage at; 0 resolves it "
               "from the host, matching feme-run"));
  cl::opt<unsigned> Workers(
      "workers", cl::init(1),
      cl::desc("The number of tile workers. Accepted for forward "
               "compatibility with the tiled scheduler (roadmap R32); this "
               "skeleton has no tiled schedule to vary, so any value "
               "behaves identically"));
  cl::opt<std::string> TileOrder(
      "tile-order", cl::init(""),
      cl::desc("The tile traversal order. Accepted for forward "
               "compatibility (roadmap R32); this skeleton has no tiled "
               "schedule yet"));
  cl::opt<bool> Reference(
      "reference",
      cl::desc("Run the scalar reference path instead of the SIMD one. "
               "Accepted for forward compatibility (roadmap R32); this "
               "skeleton executes no draws either way"));
  cl::list<std::string> Dump(
      "dump",
      cl::desc("Print attachment <name> after the last draw. May be "
               "repeated; the default is every color attachment"));
  cl::opt<std::string> Expect(
      "expect",
      cl::desc("Compare the produced attachments against a checked-in "
               "image fixture file"));
  cl::opt<double> Tolerance(
      "tolerance", cl::init(0.0),
      cl::desc("Allow a per-component absolute difference when comparing "
               "with --expect. Defaults to exact"));
  cl::opt<char> OptLevel(
      "O", cl::Prefix, cl::init('2'),
      cl::desc("The optimization level each stage is compiled at, wired "
               "the same way feme-run's -O is"));

  cl::ParseCommandLineOptions(argc, argv,
                              "FeMe software graphics executor runner\n");
  (void)Workers;
  (void)TileOrder;
  (void)Reference;

  std::optional<CodeGenOptLevel> ResolvedOptLevel =
      CodeGenOpt::parseLevel(OptLevel);
  if (!ResolvedOptLevel) {
    errs() << "feme-render: '-O" << OptLevel
           << "' is not a valid optimization level (expected 0-3)\n";
    return 1;
  }

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  ErrorOr<std::unique_ptr<MemoryBuffer>> SceneBuf =
      MemoryBuffer::getFileOrSTDIN(SceneFilename);
  if (std::error_code EC = SceneBuf.getError()) {
    errs() << "feme-render: could not open '" << SceneFilename
           << "': " << EC.message() << "\n";
    return 1;
  }

  Expected<Scene> ParsedScene = parseScene((*SceneBuf)->getBuffer());
  if (!ParsedScene) {
    errs() << "feme-render: " << toString(ParsedScene.takeError()) << "\n";
    return 1;
  }

  Expected<std::vector<AttachmentStorage>> Attachments =
      buildAttachments(ParsedScene->Attachments);
  if (!Attachments) {
    errs() << "feme-render: " << toString(Attachments.takeError()) << "\n";
    return 1;
  }

  feme::Context Ctx;
  // See feme-run.cpp's identical installation: Context itself installs no
  // default handler, so any CLI tool that wants diagnostics printed must
  // install one of its own.
  Ctx.setDiagnosticHandler([](const feme::Diagnostic &D) {
    errs() << "feme-render: "
           << (D.Severity == feme::DiagnosticSeverity::Warning ? "warning"
                                                                : "note")
           << ": " << D.Message << "\n";
  });

  if (ParsedScene->Pipeline) {
    std::vector<AttachmentFormat> Formats;
    Formats.reserve(Attachments->size());
    for (const AttachmentStorage &A : *Attachments)
      Formats.push_back(AttachmentFormat{A.Format, A.Width, A.Height});

    StringRef SceneDir = sys::path::parent_path(SceneFilename);
    Expected<GraphicsPipeline> Pipeline =
        buildPipeline(Ctx, SceneDir, *ParsedScene->Pipeline, WaveSize,
                     *ResolvedOptLevel, std::move(Formats));
    if (!Pipeline) {
      errs() << "feme-render: " << toString(Pipeline.takeError()) << "\n";
      return 1;
    }
  }

  // Draw execution (vertex/index fetch, clipping, rasterization,
  // interpolation) is roadmap R32, "Basic triangle pipeline" -- see the
  // file comment above. This skeleton renders only the cleared attachments
  // a scene with no draws describes.
  if (!ParsedScene->Draws.empty()) {
    errs() << "feme-render: executing 'draws' is not implemented yet "
              "(roadmap R32, 'Basic triangle pipeline'); this build only "
              "supports a scene with an empty (or absent) 'draws' list\n";
    return 1;
  }

  std::vector<const AttachmentStorage *> ToDump;
  if (Dump.empty()) {
    for (const AttachmentStorage &A : *Attachments)
      ToDump.push_back(&A);
  } else {
    for (const std::string &Name : Dump) {
      auto It = llvm::find_if(*Attachments, [&](const AttachmentStorage &A) {
        return A.Name == Name;
      });
      if (It == Attachments->end()) {
        errs() << "feme-render: '--dump=" << Name
               << "' names no attachment in this scene\n";
        return 1;
      }
      ToDump.push_back(&*It);
    }
  }

  if (!Expect.empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> ExpectBuf =
        MemoryBuffer::getFile(Expect);
    if (std::error_code EC = ExpectBuf.getError()) {
      errs() << "feme-render: could not open '" << Expect
             << "': " << EC.message() << "\n";
      return 1;
    }
    Expected<std::vector<ImageFixture>> ExpectedImages =
        parseImageFixtures((*ExpectBuf)->getBuffer());
    if (!ExpectedImages) {
      errs() << "feme-render: " << toString(ExpectedImages.takeError())
             << "\n";
      return 1;
    }

    for (const AttachmentStorage *A : ToDump) {
      auto It = llvm::find_if(*ExpectedImages, [&](const ImageFixture &Img) {
        return Img.Name == A->Name;
      });
      if (It == ExpectedImages->end()) {
        errs() << "feme-render: '--expect' has no image named '" << A->Name
               << "'\n";
        return 1;
      }
      if (Error E = compareAttachment(*A, *It, Tolerance)) {
        errs() << "feme-render: " << toString(std::move(E)) << "\n";
        return 1;
      }
    }
  }

  for (const AttachmentStorage *A : ToDump) {
    if (Error E = dumpAttachment(outs(), *A)) {
      errs() << "feme-render: " << toString(std::move(E)) << "\n";
      return 1;
    }
  }

  return 0;
}
