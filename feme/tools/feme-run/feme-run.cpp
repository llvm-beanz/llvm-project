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
// Scope for this milestone (see feme/docs/FeMeCPUDesign.md's Status
// section for the corresponding Deviation note): the input must already be
// idiomatic, raised LLVM IR (`.ll`/`.bc`) -- DXIL/SPIR-V import, which
// `feme::Driver` already implements, is not yet wired into this tool.
// Every resource-heap descriptor is an untyped byte buffer (raw/structured
// buffers only; no typed-buffer format conversion), matching what
// `libFeMeRuntimeCPU` and the CPU pipeline's resource-call scalarization
// exercise today.
//
// Roadmap milestone 5 adds `--reference` (see
// `feme::cpu::JITOptions::Reference`): the ground truth the CFG
// restructurization test suite diffs against.
//
//===----------------------------------------------------------------------===//

#include "feme/Core/Context.h"
#include "feme/Core/Module.h"
#include "feme/Target/CPU/JITEngine.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <vector>

using namespace llvm;
using namespace feme::cpu;

namespace {

/// One `resource-heap` entry in the heap YAML file (see the file comment
/// above): an untyped byte buffer, `Size` bytes, optionally pre-populated
/// with `Data` (little-endian `uint32` words, zero-padding any remaining
/// bytes).
struct HeapEntry {
  uint32_t Index = 0;
  uint32_t Size = 0;
  std::vector<uint32_t> Data;
};

/// The whole heap YAML file's contents.
struct HeapFile {
  std::vector<uint32_t> RootConstants;
  std::vector<HeapEntry> ResourceHeap;
};

} // namespace

LLVM_YAML_IS_SEQUENCE_VECTOR(HeapEntry)

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

template <> struct MappingTraits<HeapFile> {
  static void mapping(IO &Io, HeapFile &File) {
    Io.mapOptional("root-constants", File.RootConstants);
    Io.mapOptional("resource-heap", File.ResourceHeap);
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

HeapStorage buildHeapStorage(const HeapFile &File) {
  HeapStorage Storage;
  uint32_t MaxIndex = 0;
  for (const HeapEntry &Entry : File.ResourceHeap)
    MaxIndex = std::max(MaxIndex, Entry.Index);

  Storage.Buffers.resize(File.ResourceHeap.empty() ? 0 : MaxIndex + 1);
  Storage.Descriptors.resize(File.ResourceHeap.empty() ? 0 : MaxIndex + 1);

  for (const HeapEntry &Entry : File.ResourceHeap) {
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

/// Prints every heap entry's final contents as `uint32` words, one line
/// per entry: `heap[<index>]: <word0> <word1> ...`, for `FileCheck` to
/// match against (see the file comment above).
void printHeapContents(raw_ostream &OS, const HeapFile &File,
                       const HeapStorage &Storage) {
  for (const HeapEntry &Entry : File.ResourceHeap) {
    const std::vector<uint8_t> &Buffer = Storage.Buffers[Entry.Index];
    OS << "heap[" << Entry.Index << "]:";
    for (size_t I = 0; I + sizeof(uint32_t) <= Buffer.size();
         I += sizeof(uint32_t)) {
      uint32_t Word;
      memcpy(&Word, Buffer.data() + I, sizeof(Word));
      OS << ' ' << Word;
    }
    OS << '\n';
  }
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  cl::opt<std::string> InputFilename(cl::Positional, cl::Required,
                                     cl::desc("<input .ll/.bc file>"));
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

  SMDiagnostic Err;
  LLVMContext LLVMCtx;
  std::unique_ptr<llvm::Module> LLVMMod =
      parseIRFile(InputFilename, Err, LLVMCtx);
  if (!LLVMMod) {
    Err.print(argv[0], errs());
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

  feme::Context Ctx;
  JITOptions Opts;
  Opts.WaveSize = WaveSize;
  Opts.EntryPoint = EntryPoint;
  Opts.Reference = Reference;

  Expected<std::unique_ptr<JITEngine>> Engine = JITEngine::create(
      Ctx, feme::Module::fromLLVMIR(std::move(LLVMMod)), Opts);
  if (!Engine) {
    errs() << "feme-run: " << toString(Engine.takeError()) << "\n";
    return 1;
  }

  HeapStorage Storage = buildHeapStorage(Heap);
  std::vector<uint8_t> RootConstantBytes(Heap.RootConstants.size() *
                                         sizeof(uint32_t));
  memcpy(RootConstantBytes.data(), Heap.RootConstants.data(),
         RootConstantBytes.size());

  DispatchResources Resources;
  Resources.ResourceHeap = Storage.Descriptors;
  Resources.RootConstants = RootConstantBytes;

  if (Error E = (*Engine)->dispatch(Resources, GroupCount)) {
    errs() << "feme-run: " << toString(std::move(E)) << "\n";
    return 1;
  }

  printHeapContents(outs(), Heap, Storage);
  return 0;
}
