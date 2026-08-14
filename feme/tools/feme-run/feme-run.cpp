//===- feme-run.cpp - FeMe CPU target JIT/dispatch runner ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM
// Exceptions. See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// feme-run is the tool described in the "Command line" section of
// feme/docs/FeMeCPUDesign.md: it JITs a raised shader and dispatches it
// against a small, textual heap description, printing the resulting heap
// contents so `lit`/`FileCheck` can assert on what a shader actually
// computes rather than only on its IR shape. See "Roadmap milestone 4":
// this is the tool that makes that distinction possible for the first
// time.
//
// The input may already be idiomatic, raised LLVM IR (`.ll`/`.bc`), a DXIL
// bitcode file/DXContainer, or (roadmap step R10, see
// feme/docs/Roadmap.md's §2.4.2) a SPIR-V binary module: `loadModule` below
// sniffs which, and for DXIL/SPIR-V, runs the same import
// (`feme::DXILImporter`/`feme::SPIRVImporter`) + translation/raising
// `feme::Driver` runs before any target-specific lowering (see the "DXIL"
// and "SPIR-V" sections of feme/docs/Design.md) -- letting a test compile
// real HLSL through `clang`/DXIL's own backend and run the result straight
// through this tool (see feme/test/Tools/feme-run/HLSL), or assemble a
// `spirv` dialect MLIR module into SPIR-V with `feme-translate
// --serialize-spirv` and do the same. A SPIR-V-sourced module's bound
// `spirv.VulkanBuffer` storage-buffer resources are normalized directly by
// `feme::cpu::SPIRVResourceLoweringPass`, the SPIR-V counterpart to the
// DXIL side's `BoundResourceNormalizationPass`/`ResourceLoweringPass` pair
// (see that pass's own header comment for current scope); every other
// builtin/intrinsic the CPU pipeline lowers already accepts both formats'
// raised vocabulary uniformly (see e.g. `feme::cpu::SIMDizePass`'s
// `dx_thread_id`/`spv_thread_id` cases).
//
// Every resource-heap/binding entry defaults to an untyped raw buffer, the
// only kind milestone 11's heap file format described; roadmap step R8
// (feme/docs/Roadmap.md, "Heap YAML kind/format/stride") adds the optional
// `kind`/`format`/`stride` keys "Descriptor formats"/"Bound-resource
// normalization" in feme/docs/FeMeCPUDesign.md's richer schema sketches, so
// a test can describe a real `structured-buffer` or `typed-buffer` (with
// its storage format) instead of hand-encoding one as an untyped raw
// buffer.
//
// Roadmap milestone 5 adds `--reference` (see
// `feme::cpu::JITOptions::Reference`): the ground truth the CFG
// restructurization test suite diffs against.
//
// Roadmap milestone 11 adds the heap YAML file's `bindings` list (see
// `BindingFile`): a shader's traditionally-bound resources
// (`feme::cpu::BoundResourceNormalizationPass` normalizes them into a
// reserved heap prefix, see "Bound-resource normalization" in
// feme/docs/FeMeCPUDesign.md) are supplied there, matched by (space,
// register); `--heap`'s pre-existing `resource-heap` list now supplies only
// the shader's *logical* dynamic heap, which `feme::cpu::JITEngine::dispatch`
// places right after that reserved prefix. This replaces the
// milestone-10-era `--dxil-bind-register-resources` testing bridge (removed
// here): every HLSL test using a traditional binding now goes through this
// common path instead.
//
// Roadmap step R8 adds `--object` (see feme/docs/Roadmap.md's §2.4.5 "An
// AOT lit recipe"): treats the input as a real, already-compiled object
// file -- the output of `feme --target=<host-triple>` -- rather than IR to
// JIT-compile, loading it with `orc::LLJIT::addObjectFile` and calling its
// `feme_cpu_entry_<name>` symbol directly through `feme::cpu::runDispatch`,
// the same dispatch loop `feme::cpu::JITEngine::dispatch` uses. This is
// what lets AOT codegen (already covered by `feme --target=<host>` CLI
// tests like feme-cpu-loop.ll, which only check the compiled object
// exports the right symbol, and by `AOTDispatchTest.cpp` in `gtest`) be
// covered by `lit` too: a real object file, executed, `FileCheck`ed the
// same way the JIT path is. Unlike the JIT path, no `ResourceInfo`/IR
// metadata survives compilation for this mode to read back, so a shader
// using a traditional binding (heap YAML `bindings`) is rejected -- only
// the logical dynamic heap (`resource-heap`) is supported.
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/DXIL/DXILImporter.h"
#include "feme/Import/SPIRV/SPIRVImporter.h"
#include "feme/Target/CPU/JITEngine.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Transforms/DXIL/MetadataRaising.h"
#include "feme/Transforms/DXIL/OpRaising.h"
#include "feme/Translate/SPIRV/SPIRVToLLVMTranslator.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsDirectX.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

#include <cstdint>
#include <vector>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// One `resource-heap`/binding-range entry in the heap YAML file (see the
/// file comment above): an untyped byte buffer, `Size` bytes, optionally
/// pre-populated with `Data` (little-endian `uint32` words, zero-padding
/// any remaining bytes), plus the storage-kind/format/stride fields roadmap
/// step R8 adds (§2.4.3 in feme/docs/Roadmap.md): `Kind` (default
/// `raw-buffer`), `Format` (meaningful only for `typed-buffer`) and
/// `Stride` (meaningful only for `structured-buffer`) -- see
/// `parseResourceKind`/`parseResourceFormat` below for the accepted
/// spellings.
struct HeapEntry {
  uint32_t Index = 0;
  uint32_t Size = 0;
  std::vector<uint32_t> Data;
  std::string Kind;
  std::string Format;
  uint32_t Stride = 0;
};

/// One `bindings` entry: the host's descriptors for a shader's
/// traditionally-bound resource, matched to a
/// `feme::cpu::ResourceInfo::BoundRanges` entry by (`Space`, `Register`) --
/// see "Bound-resource normalization" in feme/docs/FeMeCPUDesign.md.
/// `Entries`' own `index` is the array element within the binding's range,
/// not a heap index -- `feme::cpu::materializeResourceHeap` places it in
/// the reserved prefix.
struct BindingFile {
  uint32_t Space = 0;
  uint32_t Register = 0;
  std::vector<HeapEntry> Entries;
};

/// The whole heap YAML file's contents.
struct HeapFile {
  std::vector<uint32_t> RootConstants;
  std::vector<HeapEntry> ResourceHeap;
  std::vector<BindingFile> Bindings;
};

} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(HeapEntry)
LLVM_YAML_IS_SEQUENCE_VECTOR(BindingFile)

namespace llvm::yaml {
/// A `std::vector<uint32_t>` sequence: `LLVM_YAML_IS_SEQUENCE_VECTOR`
/// rejects fundamental element types (see its own comment), so this is
/// spelled out directly instead.
template <> struct SequenceTraits<std::vector<uint32_t>> {
  static size_t size(IO &Io, std::vector<uint32_t> &Seq) { return Seq.size(); }
  static uint32_t &element(IO &Io, std::vector<uint32_t> &Seq, size_t Index) {
    if (Index >= Seq.size())
      Seq.resize(Index + 1);
    return Seq[Index];
  }
};
} // namespace llvm::yaml

namespace llvm::yaml {
template <> struct MappingTraits<HeapEntry> {
  static void mapping(IO &Io, HeapEntry &Entry) {
    Io.mapRequired("index", Entry.Index);
    Io.mapOptional("size", Entry.Size, 0u);
    Io.mapOptional("data", Entry.Data);
    Io.mapOptional("kind", Entry.Kind);
    Io.mapOptional("format", Entry.Format);
    Io.mapOptional("stride", Entry.Stride, 0u);
  }
};

template <> struct MappingTraits<BindingFile> {
  static void mapping(IO &Io, BindingFile &Binding) {
    Io.mapRequired("space", Binding.Space);
    Io.mapRequired("register", Binding.Register);
    Io.mapOptional("entries", Binding.Entries);
  }
};

template <> struct MappingTraits<HeapFile> {
  static void mapping(IO &Io, HeapFile &File) {
    Io.mapOptional("root-constants", File.RootConstants);
    Io.mapOptional("resource-heap", File.ResourceHeap);
    Io.mapOptional("bindings", File.Bindings);
  }
};
} // namespace llvm::yaml

namespace {

/// Reads and parses \p Filename as a heap YAML file (see the file comment
/// above), returning its contents or an `Error` on a read/parse failure.
Expected<HeapFile> readHeapFile(StringRef Filename) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(Filename);
  if (std::error_code EC = BufOrErr.getError())
    return createStringError(EC, "could not open '%s': %s",
                             Filename.str().c_str(), EC.message().c_str());

  HeapFile File;
  yaml::Input Yin((*BufOrErr)->getBuffer());
  Yin >> File;
  if (Yin.error())
    return createStringError(Yin.error(), "could not parse '%s'",
                             Filename.str().c_str());
  return File;
}

/// Builds each `HeapEntry`'s backing storage (owned for the dispatch's
/// duration) and the `FemeDescriptor`s pointing at it: `Kind`/`Format`/
/// `Stride` (roadmap step R8, §2.4.3) let an entry describe a real
/// `structured-buffer`/`typed-buffer` instead of always being an
/// unstructured raw buffer (see the file comment above).
struct HeapStorage {
  // One buffer per entry, indexed the same way `Descriptors` is; kept
  // alive here since `FemeDescriptor::Data` merely points into it.
  std::vector<std::vector<uint8_t>> Buffers;
  std::vector<FemeDescriptor> Descriptors;
};

/// Parses a heap YAML entry's `kind` string into `feme::cpu::ResourceKind`,
/// defaulting an empty string to `Raw` (the only kind milestone 11's heap
/// file format described, kept as the default for backward compatibility --
/// see the file comment above). Returns an `Error` for any other,
/// unrecognized spelling.
Expected<ResourceKind> parseResourceKind(StringRef Kind) {
  if (Kind.empty() || Kind == "raw-buffer")
    return ResourceKind::Raw;
  if (Kind == "structured-buffer")
    return ResourceKind::Structured;
  if (Kind == "typed-buffer")
    return ResourceKind::Typed;
  if (Kind == "cbuffer")
    return ResourceKind::CBuffer;
  return createStringError(inconvertibleErrorCode(),
                           "unknown heap entry 'kind': '%s'",
                           Kind.str().c_str());
}

/// Parses a heap YAML entry's `format` string (meaningful only for a
/// `typed-buffer` entry) into `feme::cpu::ResourceFormat`: the lowercase,
/// underscore-separated spelling of the enumerator's own name, e.g.
/// `r32g32b32a32_float` for `ResourceFormat::R32G32B32A32_FLOAT` --
/// matching the literal spelling "Command line" in
/// feme/docs/FeMeCPUDesign.md sketches. An empty string parses as
/// `Unknown`. Returns an `Error` for any other, unrecognized spelling.
Expected<ResourceFormat> parseResourceFormat(StringRef Format) {
  ResourceFormat Result =
      StringSwitch<ResourceFormat>(Format)
          .Cases({"", "unknown"}, ResourceFormat::Unknown)
          .Case("r32_float", ResourceFormat::R32_FLOAT)
          .Case("r32g32_float", ResourceFormat::R32G32_FLOAT)
          .Case("r32g32b32_float", ResourceFormat::R32G32B32_FLOAT)
          .Case("r32g32b32a32_float", ResourceFormat::R32G32B32A32_FLOAT)
          .Case("r32_uint", ResourceFormat::R32_UINT)
          .Case("r32g32_uint", ResourceFormat::R32G32_UINT)
          .Case("r32g32b32_uint", ResourceFormat::R32G32B32_UINT)
          .Case("r32g32b32a32_uint", ResourceFormat::R32G32B32A32_UINT)
          .Case("r32_sint", ResourceFormat::R32_SINT)
          .Case("r32g32_sint", ResourceFormat::R32G32_SINT)
          .Case("r32g32b32_sint", ResourceFormat::R32G32B32_SINT)
          .Case("r32g32b32a32_sint", ResourceFormat::R32G32B32A32_SINT)
          .Case("r8g8b8a8_unorm", ResourceFormat::R8G8B8A8_UNORM)
          .Case("r8g8b8a8_snorm", ResourceFormat::R8G8B8A8_SNORM)
          .Case("r8g8b8a8_uint", ResourceFormat::R8G8B8A8_UINT)
          .Case("r8g8b8a8_sint", ResourceFormat::R8G8B8A8_SINT)
          .Case("r8g8b8a8_unorm_srgb", ResourceFormat::R8G8B8A8_UNORM_SRGB)
          .Case("r16g16b16a16_float", ResourceFormat::R16G16B16A16_FLOAT)
          .Case("r16g16b16a16_unorm", ResourceFormat::R16G16B16A16_UNORM)
          .Case("r16g16b16a16_snorm", ResourceFormat::R16G16B16A16_SNORM)
          .Case("r16g16b16a16_uint", ResourceFormat::R16G16B16A16_UINT)
          .Case("r16g16b16a16_sint", ResourceFormat::R16G16B16A16_SINT)
          .Case("r11g11b10_float", ResourceFormat::R11G11B10_FLOAT)
          .Case("r10g10b10a2_unorm", ResourceFormat::R10G10B10A2_UNORM)
          .Case("r10g10b10a2_uint", ResourceFormat::R10G10B10A2_UINT)
          .Default(ResourceFormat::Unknown);
  if (Result == ResourceFormat::Unknown && !Format.empty() &&
      Format != "unknown")
    return createStringError(inconvertibleErrorCode(),
                             "unknown heap entry 'format': '%s'",
                             Format.str().c_str());
  return Result;
}

/// Builds \p Entries' backing storage the same way `buildHeapStorage`
/// builds `resource-heap`'s, densely indexed by each entry's own `index`
/// field (the array element within a `bindings` entry's range, for that
/// caller -- see `BindingFile`'s own comment).
Expected<HeapStorage> buildEntryStorage(ArrayRef<HeapEntry> Entries) {
  HeapStorage Storage;
  uint32_t MaxIndex = 0;
  for (const HeapEntry &Entry : Entries)
    MaxIndex = std::max(MaxIndex, Entry.Index);

  Storage.Buffers.resize(Entries.empty() ? 0 : MaxIndex + 1);
  Storage.Descriptors.resize(Entries.empty() ? 0 : MaxIndex + 1);

  for (const HeapEntry &Entry : Entries) {
    Expected<ResourceKind> Kind = parseResourceKind(Entry.Kind);
    if (!Kind)
      return Kind.takeError();
    Expected<ResourceFormat> Format = parseResourceFormat(Entry.Format);
    if (!Format)
      return Format.takeError();

    uint32_t ByteSize =
        std::max<uint32_t>(Entry.Size, Entry.Data.size() * sizeof(uint32_t));
    std::vector<uint8_t> &Buffer = Storage.Buffers[Entry.Index];
    Buffer.assign(ByteSize, 0);
    size_t CopySize =
        std::min<size_t>(Buffer.size(), Entry.Data.size() * sizeof(uint32_t));
    // `Entry.Data.data()` is null when `Entry.Data` is empty (no initial
    // data in the heap file); memcpy's source parameter is declared
    // never-null, so guard the call rather than pass a null pointer even
    // though `CopySize` would be 0.
    if (CopySize > 0)
      memcpy(Buffer.data(), Entry.Data.data(), CopySize);

    FemeDescriptor &Desc = Storage.Descriptors[Entry.Index];
    Desc = FemeDescriptor{};
    Desc.Data = Buffer.data();
    Desc.SizeInBytes = Buffer.size();
    Desc.Stride = Entry.Stride;
    Desc.Format = static_cast<uint32_t>(*Format);
    Desc.Kind = static_cast<uint32_t>(*Kind);
    Desc.Flags = FEME_DESCRIPTOR_UAV;
  }
  return Storage;
}

Expected<HeapStorage> buildHeapStorage(const HeapFile &File) {
  return buildEntryStorage(File.ResourceHeap);
}

/// One `bindings` entry's backing storage: `Entries`' buffers/descriptors
/// (see `buildEntryStorage`) plus the (space, register) identity a
/// `feme::cpu::BoundResourceBinding` is matched by.
struct BindingStorage {
  uint32_t Space = 0;
  uint32_t Register = 0;
  HeapStorage Entries;
};

Expected<std::vector<BindingStorage>>
buildBindingStorage(const HeapFile &File) {
  std::vector<BindingStorage> Storage;
  Storage.reserve(File.Bindings.size());
  for (const BindingFile &Binding : File.Bindings) {
    Expected<HeapStorage> Entries = buildEntryStorage(Binding.Entries);
    if (!Entries)
      return Entries.takeError();
    Storage.push_back(
        BindingStorage{Binding.Space, Binding.Register, std::move(*Entries)});
  }
  return Storage;
}

/// Builds the `feme::cpu::BoundResourceBinding` array `JITEngine::dispatch`
/// expects from \p Storage, referencing (not copying) each binding's own
/// descriptor storage.
std::vector<BoundResourceBinding>
toBoundResourceBindings(const std::vector<BindingStorage> &Storage) {
  std::vector<BoundResourceBinding> Bindings;
  Bindings.reserve(Storage.size());
  for (const BindingStorage &Binding : Storage)
    Bindings.push_back(BoundResourceBinding{Binding.Space, Binding.Register,
                                            Binding.Entries.Descriptors});
  return Bindings;
}

/// Prints every heap entry's final contents as `uint32` words, one line
/// per entry: `heap[<index>]: <word0> <word1> ...` for a `resource-heap`
/// entry, `binding[<space>:<register>][<index>]: <word0> <word1> ...` for a
/// `bindings` entry, for `FileCheck` to match against (see the file
/// comment above).
void printHeapContents(raw_ostream &OS, const HeapFile &File,
                       const HeapStorage &Storage,
                       const std::vector<BindingStorage> &BindingsStorage) {
  auto PrintBuffer = [&](const std::vector<uint8_t> &Buffer) {
    for (size_t I = 0; I + sizeof(uint32_t) <= Buffer.size();
         I += sizeof(uint32_t)) {
      uint32_t Word;
      memcpy(&Word, Buffer.data() + I, sizeof(Word));
      OS << ' ' << Word;
    }
    OS << '\n';
  };

  for (const HeapEntry &Entry : File.ResourceHeap) {
    OS << "heap[" << Entry.Index << "]:";
    PrintBuffer(Storage.Buffers[Entry.Index]);
  }
  for (auto [Binding, BindingFile] : zip(BindingsStorage, File.Bindings)) {
    for (const HeapEntry &Entry : BindingFile.Entries) {
      OS << "binding[" << Binding.Space << ":" << Binding.Register << "]["
         << Entry.Index << "]:";
      PrintBuffer(Binding.Entries.Buffers[Entry.Index]);
    }
  }
}

/// Whether \p Buffer looks like a DXIL bitcode file or DXContainer, the same
/// sniff `feme::Driver`'s own format detection uses (see the "DXIL" section
/// of feme/docs/Design.md): a `DXContainer` starts with the magic "DXBC"
/// (the format predates the DXIL name), while raw DXIL is plain LLVM
/// bitcode, optionally with the standard bitcode wrapper header.
bool looksLikeDXIL(MemoryBufferRef Buffer) {
  StringRef Data = Buffer.getBuffer();
  return Data.starts_with("DXBC") ||
         isBitcode(reinterpret_cast<const unsigned char *>(Data.data()),
                   reinterpret_cast<const unsigned char *>(Data.data() +
                                                           Data.size()));
}

/// Whether \p Buffer looks like a SPIR-V binary module, the same sniff
/// `feme::Driver`'s own format detection uses (see the "SPIR-V" section of
/// feme/docs/Design.md): a stream of 32-bit words beginning with a fixed
/// magic number, in either byte order depending on the endianness its
/// producer chose (see the SPIR-V specification's "Physical Layout of a
/// SPIR-V Module and Instruction").
bool looksLikeSPIRV(MemoryBufferRef Buffer) {
  StringRef Data = Buffer.getBuffer();
  if (Data.size() < sizeof(uint32_t))
    return false;
  uint32_t Magic;
  memcpy(&Magic, Data.data(), sizeof(Magic));
  return Magic == 0x07230203u || Magic == 0x03022307u;
}

/// Clears \p M's target triple/data layout and drops its
/// `llvm.module.flags`: neither format's own module flavor has any meaning
/// to the FeMe CPU target's JIT (see `feme::Driver::run`: retargeting to
/// any other target, CPU included, replaces both once codegen begins), and
/// Clang's HLSL front end stamps ordinary host-compiler module flags (e.g.
/// "frame-pointer") that would otherwise only ever produce a harmless but
/// noisy "conflicting values" warning once linked against
/// `libFeMeRuntimeCPU`'s own (different) values for them. Leaving both
/// target-agnostic matches every raised `.ll` fixture (see e.g.
/// feme/test/Tools/feme-run/thread-id-store.ll, which carries no `target
/// triple`/`target datalayout` at all) so `feme::cpu::JITEngine` -- which
/// never itself sets either -- treats every importer's output exactly the
/// same way.
void clearHostAgnosticMetadata(llvm::Module &M) {
  M.setTargetTriple(llvm::Triple());
  M.setDataLayout(llvm::DataLayout());
  if (llvm::NamedMDNode *ModuleFlags = M.getNamedMetadata("llvm.module.flags"))
    M.eraseNamedMetadata(ModuleFlags);
}

/// Loads \p Filename as the raised LLVM IR the JIT engine expects. Already-
/// raised, idiomatic LLVM IR (`.ll`/`.bc`) is parsed directly, matching
/// `feme-run`'s original, milestone-4 scope; a DXIL bitcode file or
/// DXContainer is imported and raised the same way `feme::Driver` raises
/// one before any target-specific lowering (`feme::dxil::OpRaisingPass`
/// undoes the `dx.op.*` calling convention, then
/// `feme::dxil::MetadataRaisingPass` recovers the shader model, entry
/// points, and thread-group dimensions from `dx.*` metadata into the
/// `hlsl.*` function attributes this tool's own pipeline reads them from --
/// see the DXIL section of feme/docs/Design.md) -- letting a test compile
/// real HLSL through `clang`/DXIL's own backend and run the result straight
/// through this tool. A SPIR-V binary module is imported
/// (`feme::SPIRVImporter`, reusing MLIR's own `spirv` dialect deserializer)
/// and translated straight to LLVM IR (`feme::SPIRVToLLVMTranslator`);
/// unlike DXIL, no further raising pass runs here -- the translator's own
/// `feme::spirv::createConvertSPIRVToLLVMPass` (see the "SPIR-V -> MLIR llvm
/// dialect -> LLVM IR" section of feme/docs/Design.md) already recovers the
/// entry point/thread-group-size `hlsl.*` function attributes DXIL's
/// `MetadataRaisingPass` recovers separately, and leaves resource access in
/// the raised, format-agnostic `llvm.spv.*` vocabulary
/// `feme::cpu::SPIRVResourceLoweringPass` (part of the CPU pipeline itself,
/// see its own header comment) normalizes, mirroring how DXIL's raised
/// `llvm.dx.*` vocabulary is normalized there too.
Expected<feme::Module> loadModule(StringRef Filename, feme::Context &Ctx) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFileOrSTDIN(Filename, /*IsText=*/false);
  if (std::error_code EC = BufOrErr.getError())
    return createStringError(EC, "could not open '%s': %s",
                             Filename.str().c_str(), EC.message().c_str());

  if (looksLikeDXIL((*BufOrErr)->getMemBufferRef())) {
    feme::DXILImporter Importer;
    feme::ImportOptions ImportOpts;
    Expected<feme::Module> Imported =
        Importer.import((*BufOrErr)->getMemBufferRef(), ImportOpts, Ctx);
    if (!Imported)
      return Imported.takeError();

    llvm::Module &M = Imported->getLLVMModule();
    ModuleAnalysisManager MAM;
    feme::dxil::OpRaisingPass().run(M, MAM);
    feme::dxil::MetadataRaisingPass().run(M, MAM);

    clearHostAgnosticMetadata(M);
    return Imported;
  }

  if (looksLikeSPIRV((*BufOrErr)->getMemBufferRef())) {
    feme::SPIRVImporter Importer;
    feme::ImportOptions ImportOpts;
    Expected<feme::Module> Imported =
        Importer.import((*BufOrErr)->getMemBufferRef(), ImportOpts, Ctx);
    if (!Imported)
      return Imported.takeError();

    feme::SPIRVToLLVMTranslator ToLLVMIR;
    Expected<feme::Module> AsLLVMIR =
        ToLLVMIR.translate(std::move(*Imported), Ctx);
    if (!AsLLVMIR)
      return AsLLVMIR.takeError();

    clearHostAgnosticMetadata(AsLLVMIR->getLLVMModule());
    return AsLLVMIR;
  }

  SMDiagnostic Err;
  std::unique_ptr<llvm::Module> LLVMMod =
      parseIR((*BufOrErr)->getMemBufferRef(), Err, Ctx.getLLVMContext());
  if (!LLVMMod) {
    std::string Message;
    raw_string_ostream OS(Message);
    Err.print("feme-run", OS);
    return createStringError(inconvertibleErrorCode(), "%s", Message.c_str());
  }
  return feme::Module::fromLLVMIR(std::move(LLVMMod));
}

/// Runs `--object` mode (see the file comment above): loads \p Filename as
/// a real compiled object file, resolves \p EntryPoint's (or "main"'s)
/// `feme_cpu_entry_<name>` symbol in it, and dispatches \p GroupCount
/// groups against it through `feme::cpu::runDispatch`, printing the
/// resulting heap contents the same way the JIT path's `main` does.
/// \p Heap's `bindings` must be empty: with no IR/metadata surviving
/// compilation, this mode has no `ResourceInfo` to place a traditionally-
/// bound resource's reserved prefix, so only `Heap.ResourceHeap`'s logical
/// dynamic heap is usable (an empty `ResourceInfo` below means no prefix
/// is reserved at all).
Error runObjectMode(StringRef Filename, StringRef EntryPoint,
                    const HeapFile &Heap, std::array<uint32_t, 3> GroupCount,
                    const HeapStorage &Storage) {
  if (!Heap.Bindings.empty())
    return createStringError(
        inconvertibleErrorCode(),
        "'--object' does not support traditional bindings (heap YAML "
        "'bindings'): no compiled-object metadata records their reserved "
        "heap prefix; describe every resource under 'resource-heap' "
        "instead");

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFileOrSTDIN(Filename, /*IsText=*/false);
  if (std::error_code EC = BufOrErr.getError())
    return createStringError(EC, "could not open '%s': %s",
                             Filename.str().c_str(), EC.message().c_str());

  Expected<std::unique_ptr<orc::LLJIT>> JIT = orc::LLJITBuilder().create();
  if (!JIT)
    return JIT.takeError();
  if (Error E = (*JIT)->addObjectFile(std::move(*BufOrErr)))
    return E;

  std::string SymbolName =
      "feme_cpu_entry_" + (EntryPoint.empty() ? "main" : EntryPoint).str();
  Expected<orc::ExecutorAddr> EntryAddr = (*JIT)->lookup(SymbolName);
  if (!EntryAddr)
    return EntryAddr.takeError();

  DispatchResources Resources;
  Resources.ResourceHeap = Storage.Descriptors;
  runDispatch(EntryAddr->toPtr<EntryPointFn>(), ResourceInfo{}, Resources,
              GroupCount);
  return Error::success();
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  cl::opt<std::string> InputFilename(
      cl::Positional, cl::Required,
      cl::desc("<input .ll/.bc file, DXIL bitcode/DXContainer, or SPIR-V "
               "binary module>"));
  cl::opt<std::string> HeapFilename(
      "heap", cl::desc("The resource-heap YAML file (see feme-run's own "
                       "file comment)"));
  cl::opt<unsigned> WaveSize(
      "wave-size", cl::init(0),
      cl::desc("The wave size to compile at; 0 resolves it from the "
               "module/host"));
  cl::opt<std::string> GroupCountStr(
      "groups", cl::init("1,1,1"),
      cl::desc("The dispatch's group count, as 'X,Y,Z'"));
  cl::opt<std::string> EntryPoint(
      "entry-point",
      cl::desc("The compute entry point to run, if the module has more "
               "than one"));
  cl::opt<bool> Reference(
      "reference",
      cl::desc("Run the shader one invocation at a time through the "
               "unwidened module instead of Phases 3/4 -- the ground truth "
               "the CFG restructurization test suite diffs against (see "
               "the 'CFG restructurization test suite' section of "
               "feme/docs/FeMeCPUDesign.md). --wave-size is ignored."));
  cl::opt<bool> ObjectMode(
      "object",
      cl::desc("Treat <input> as a real compiled object file (the output "
               "of 'feme --target=<host-triple>') instead of IR/DXIL to "
               "JIT-compile, and dispatch its already-compiled "
               "feme_cpu_entry_<name> symbol directly (see feme-run's own "
               "file comment). --wave-size/--reference are ignored; heap "
               "YAML 'bindings' are rejected."));

  cl::ParseCommandLineOptions(argc, argv,
                              "FeMe CPU target JIT/dispatch runner\n");

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  HeapFile Heap;
  if (!HeapFilename.empty()) {
    Expected<HeapFile> Parsed = readHeapFile(HeapFilename);
    if (!Parsed) {
      errs() << "feme-run: " << toString(Parsed.takeError()) << "\n";
      return 1;
    }
    Heap = std::move(*Parsed);
  }

  SmallVector<StringRef, 3> GroupCountParts;
  StringRef(GroupCountStr).split(GroupCountParts, ',');
  std::array<uint32_t, 3> GroupCount{1, 1, 1};
  for (unsigned I = 0; I != std::min<size_t>(3, GroupCountParts.size()); ++I)
    if (GroupCountParts[I].getAsInteger(10, GroupCount[I])) {
      errs() << "feme-run: '--groups' must be three comma-separated "
                "integers\n";
      return 1;
    }

  Expected<HeapStorage> Storage = buildHeapStorage(Heap);
  if (!Storage) {
    errs() << "feme-run: " << toString(Storage.takeError()) << "\n";
    return 1;
  }

  if (ObjectMode) {
    if (Error E = runObjectMode(InputFilename, EntryPoint, Heap, GroupCount,
                                *Storage)) {
      errs() << "feme-run: " << toString(std::move(E)) << "\n";
      return 1;
    }
    printHeapContents(outs(), Heap, *Storage, /*BindingsStorage=*/{});
    return 0;
  }

  feme::Context Ctx;
  Expected<feme::Module> Mod = loadModule(InputFilename, Ctx);
  if (!Mod) {
    errs() << "feme-run: " << toString(Mod.takeError()) << "\n";
    return 1;
  }

  JITOptions Opts;
  Opts.WaveSize = WaveSize;
  Opts.EntryPoint = EntryPoint;
  Opts.Reference = Reference;

  Expected<std::unique_ptr<JITEngine>> Engine =
      JITEngine::create(Ctx, std::move(*Mod), Opts);
  if (!Engine) {
    errs() << "feme-run: " << toString(Engine.takeError()) << "\n";
    return 1;
  }

  Expected<std::vector<BindingStorage>> BindingsStorage =
      buildBindingStorage(Heap);
  if (!BindingsStorage) {
    errs() << "feme-run: " << toString(BindingsStorage.takeError()) << "\n";
    return 1;
  }
  std::vector<BoundResourceBinding> Bindings =
      toBoundResourceBindings(*BindingsStorage);
  std::vector<uint8_t> RootConstantBytes(Heap.RootConstants.size() *
                                         sizeof(uint32_t));
  // Guard against a null `data()` on both sides when `RootConstants` is
  // empty (see `buildEntryStorage`'s equivalent memcpy guard above).
  if (!RootConstantBytes.empty())
    memcpy(RootConstantBytes.data(), Heap.RootConstants.data(),
           RootConstantBytes.size());

  DispatchResources Resources;
  Resources.ResourceHeap = Storage->Descriptors;
  Resources.BoundResources = Bindings;
  Resources.RootConstants = RootConstantBytes;

  if (Error E = (*Engine)->dispatch(Resources, GroupCount)) {
    errs() << "feme-run: " << toString(std::move(E)) << "\n";
    return 1;
  }

  printHeapContents(outs(), Heap, *Storage, *BindingsStorage);
  return 0;
}
