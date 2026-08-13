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
// The input may either already be idiomatic, raised LLVM IR (`.ll`/`.bc`),
// or a DXIL bitcode file/DXContainer: `loadModule` below sniffs which, and
// for DXIL, runs the same import + op/metadata raising `feme::Driver` runs
// before any target-specific lowering (see the "DXIL" section of
// feme/docs/Design.md) -- closing the "DXIL/SPIR-V import ... is not yet
// wired into this tool" gap this file's own comment used to note for that
// format, and letting a test compile real HLSL through `clang`/DXIL's own
// backend and run the result straight through this tool (see
// feme/test/Tools/feme-run/HLSL). SPIR-V import remains unwired: see the
// "SPIR-V" deviation note this milestone's update to
// feme/docs/FeMeCPUDesign.md's Status section adds.
//
// Every resource-heap descriptor is an untyped byte buffer (raw/structured
// buffers only; no typed-buffer format conversion), matching what
// `libFeMeRuntimeCPU` and the CPU pipeline's resource-call scalarization
// exercise today.
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
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Import/DXIL/DXILImporter.h"
#include "feme/Target/CPU/JITEngine.h"
#include "feme/Target/CPU/ResourceHeap.h"
#include "feme/Transforms/DXIL/MetadataRaising.h"
#include "feme/Transforms/DXIL/OpRaising.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Bitcode/BitcodeReader.h"
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
/// any remaining bytes).
struct HeapEntry {
  uint32_t Index = 0;
  uint32_t Size = 0;
  std::vector<uint32_t> Data;
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
/// duration) and the `FemeDescriptor`s pointing at it: every entry is an
/// unstructured, host-writable raw buffer (`ResourceKind::Raw`,
/// `FEME_DESCRIPTOR_UAV`), the only kind this milestone's heap file format
/// describes (see the file comment above).
struct HeapStorage {
  // One buffer per entry, indexed the same way `Descriptors` is; kept
  // alive here since `FemeDescriptor::Data` merely points into it.
  std::vector<std::vector<uint8_t>> Buffers;
  std::vector<FemeDescriptor> Descriptors;
};

/// Builds \p Entries' backing storage the same way `buildHeapStorage`
/// builds `resource-heap`'s, densely indexed by each entry's own `index`
/// field (the array element within a `bindings` entry's range, for that
/// caller -- see `BindingFile`'s own comment).
HeapStorage buildEntryStorage(ArrayRef<HeapEntry> Entries) {
  HeapStorage Storage;
  uint32_t MaxIndex = 0;
  for (const HeapEntry &Entry : Entries)
    MaxIndex = std::max(MaxIndex, Entry.Index);

  Storage.Buffers.resize(Entries.empty() ? 0 : MaxIndex + 1);
  Storage.Descriptors.resize(Entries.empty() ? 0 : MaxIndex + 1);

  for (const HeapEntry &Entry : Entries) {
    uint32_t ByteSize =
        std::max<uint32_t>(Entry.Size, Entry.Data.size() * sizeof(uint32_t));
    std::vector<uint8_t> &Buffer = Storage.Buffers[Entry.Index];
    Buffer.assign(ByteSize, 0);
    memcpy(
        Buffer.data(), Entry.Data.data(),
        std::min<size_t>(Buffer.size(), Entry.Data.size() * sizeof(uint32_t)));

    FemeDescriptor &Desc = Storage.Descriptors[Entry.Index];
    Desc = FemeDescriptor{};
    Desc.Data = Buffer.data();
    Desc.SizeInBytes = Buffer.size();
    Desc.Kind = static_cast<uint32_t>(ResourceKind::Raw);
    Desc.Flags = FEME_DESCRIPTOR_UAV;
  }
  return Storage;
}

HeapStorage buildHeapStorage(const HeapFile &File) {
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

std::vector<BindingStorage> buildBindingStorage(const HeapFile &File) {
  std::vector<BindingStorage> Storage;
  Storage.reserve(File.Bindings.size());
  for (const BindingFile &Binding : File.Bindings)
    Storage.push_back(BindingStorage{Binding.Space, Binding.Register,
                                     buildEntryStorage(Binding.Entries)});
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
/// through this tool. See the file comment above for what remains unwired
/// (SPIR-V import).
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

    // DXIL's own module triple/data layout has no meaning to the FeMe CPU
    // target's JIT (see `feme::Driver::run`: retargeting to any other
    // target, CPU included, replaces both once codegen begins). Clearing
    // them leaves the module target-agnostic, matching every raised `.ll`
    // fixture (see e.g. feme/test/Tools/feme-run/thread-id-store.ll, which
    // carries no `target triple`/`target datalayout` at all) so
    // `feme::cpu::JITEngine` -- which never itself sets either -- treats it
    // exactly the same way regardless of which importer produced it.
    M.setTargetTriple(llvm::Triple());
    M.setDataLayout(llvm::DataLayout());
    // Clang's HLSL front end also stamps ordinary host-compiler module
    // flags (e.g. "frame-pointer") that mean nothing to the FeMe CPU
    // target's JIT and, when linked against `libFeMeRuntimeCPU`'s own
    // (different) values for them, would only ever produce a harmless but
    // noisy "conflicting values" warning; drop them for the same reason the
    // triple/data layout above are cleared.
    if (llvm::NamedMDNode *ModuleFlags =
            M.getNamedMetadata("llvm.module.flags"))
      M.eraseNamedMetadata(ModuleFlags);

    return Imported;
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

} // namespace

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  cl::opt<std::string> InputFilename(
      cl::Positional, cl::Required,
      cl::desc("<input .ll/.bc file, or DXIL bitcode/DXContainer>"));
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

  cl::ParseCommandLineOptions(argc, argv,
                              "FeMe CPU target JIT/dispatch runner\n");

  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();

  feme::Context Ctx;
  Expected<feme::Module> Mod = loadModule(InputFilename, Ctx);
  if (!Mod) {
    errs() << "feme-run: " << toString(Mod.takeError()) << "\n";
    return 1;
  }

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

  HeapStorage Storage = buildHeapStorage(Heap);
  std::vector<BindingStorage> BindingsStorage = buildBindingStorage(Heap);
  std::vector<BoundResourceBinding> Bindings =
      toBoundResourceBindings(BindingsStorage);
  std::vector<uint8_t> RootConstantBytes(Heap.RootConstants.size() *
                                         sizeof(uint32_t));
  memcpy(RootConstantBytes.data(), Heap.RootConstants.data(),
         RootConstantBytes.size());

  DispatchResources Resources;
  Resources.ResourceHeap = Storage.Descriptors;
  Resources.BoundResources = Bindings;
  Resources.RootConstants = RootConstantBytes;

  if (Error E = (*Engine)->dispatch(Resources, GroupCount)) {
    errs() << "feme-run: " << toString(std::move(E)) << "\n";
    return 1;
  }

  printHeapContents(outs(), Heap, Storage, BindingsStorage);
  return 0;
}
