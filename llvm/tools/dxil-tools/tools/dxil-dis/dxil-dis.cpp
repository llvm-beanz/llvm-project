//===-- llvm-dis.cpp - The low-level LLVM disassembler --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-dis [options]      - Read LLVM bitcode from stdin, write asm to stdout
//  llvm-dis [options] x.bc - Read LLVM bitcode from the x.bc file, write asm
//                            to the x.ll file.
//  Options:
//      --help   - Output information about command line switches
//
//===----------------------------------------------------------------------===//

#include "dxil/Bitcode/ReaderWriter.h"
#include "dxil/IR/AssemblyAnnotationWriter.h"
#include "dxil/IR/DebugInfo.h"
#include "dxil/IR/DiagnosticInfo.h"
#include "dxil/IR/DiagnosticPrinter.h"
#include "dxil/IR/IntrinsicInst.h"
#include "dxil/IR/LLVMContext.h"
#include "dxil/IR/Module.h"
#include "dxil/IR/Type.h"
#include "llvm/Object/DXContainer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include <system_error>
using namespace llvm;
using dxil::AssemblyAnnotationWriter;
using dxil::DbgDeclareInst;
using dxil::DbgValueInst;
using dxil::DebugLoc;
using dxil::DiagnosticInfo;
using dxil::DiagnosticPrinterRawOStream;
using dxil::DILocation;
using dxil::Function;
using dxil::Instruction;
using dxil::LLVMContext;
using dxil::Module;
using dxil::Value;

static cl::opt<std::string>
    InputFilename(cl::Positional, cl::desc("<input bitcode>"), cl::init("-"));

static cl::opt<std::string> OutputFilename("o",
                                           cl::desc("Override output filename"),
                                           cl::value_desc("filename"));

static cl::opt<bool> Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool> DontPrint("disable-output",
                               cl::desc("Don't output the .ll file"),
                               cl::Hidden);

static cl::opt<bool>
    ShowAnnotations("show-annotations",
                    cl::desc("Add informational comments to the .ll file"));

static cl::opt<bool> PreserveAssemblyUseListOrder(
    "preserve-ll-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM assembly."),
    cl::init(false), cl::Hidden);

namespace {

static void printDebugLoc(const DebugLoc &DL, formatted_raw_ostream &OS) {
  OS << DL.getLine() << ":" << DL.getCol();
  if (DILocation *IDL = DL.getInlinedAt()) {
    OS << "@";
    printDebugLoc(IDL, OS);
  }
}
class CommentWriter : public AssemblyAnnotationWriter {
public:
  void emitFunctionAnnot(const Function *F,
                         formatted_raw_ostream &OS) override {
    OS << "; [#uses=" << F->getNumUses() << ']'; // Output # uses
    OS << '\n';
  }
  void printInfoComment(const Value &V, formatted_raw_ostream &OS) override {
    bool Padded = false;
    if (!V.getType()->isVoidTy()) {
      OS.PadToColumn(50);
      Padded = true;
      // Output # uses and type
      OS << "; [#uses=" << V.getNumUses() << " type=" << *V.getType() << "]";
    }
    if (const Instruction *I = dyn_cast<Instruction>(&V)) {
      if (const DebugLoc &DL = I->getDebugLoc()) {
        if (!Padded) {
          OS.PadToColumn(50);
          Padded = true;
          OS << ";";
        }
        OS << " [debug line = ";
        printDebugLoc(DL, OS);
        OS << "]";
      }
      if (const DbgDeclareInst *DDI = dyn_cast<DbgDeclareInst>(I)) {
        if (!Padded) {
          OS.PadToColumn(50);
          OS << ";";
        }
        OS << " [debug variable = " << DDI->getVariable()->getName() << "]";
      } else if (const DbgValueInst *DVI = dyn_cast<DbgValueInst>(I)) {
        if (!Padded) {
          OS.PadToColumn(50);
          OS << ";";
        }
        OS << " [debug variable = " << DVI->getVariable()->getName() << "]";
      }
    }
  }
};

} // namespace

static void diagnosticHandler(const DiagnosticInfo *DI, void *Context) {
  raw_ostream &OS = errs();
  OS << (char *)Context << ": ";
  switch (DI->getSeverity()) {
  case dxil::DS_Error:
    OS << "error: ";
    break;
  case dxil::DS_Warning:
    OS << "warning: ";
    break;
  case dxil::DS_Remark:
    OS << "remark: ";
    break;
  case dxil::DS_Note:
    OS << "note: ";
    break;
  }

  DiagnosticPrinterRawOStream DP(OS);
  DI->print(DP);
  OS << '\n';

  if (DI->getSeverity() == dxil::DS_Error)
    exit(1);
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  LLVMContext Context;

  Context.setDiagnosticHandler(diagnosticHandler, argv[0]);

  cl::ParseCommandLineOptions(argc, argv, "DXIL .bc -> .ll disassembler\n");

  std::string ErrorMessage;
  std::unique_ptr<Module> M;

  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename);
  if (std::error_code EC = FileOrErr.getError()) {
    errs() << argv[0] << ": "
           << "Could not open file '" << InputFilename << "': " << EC.message()
           << '\n';
    return 1;
  }

  std::unique_ptr<MemoryBuffer> &Buf = FileOrErr.get();
  MemoryBufferRef BitcodeData = Buf->getMemBufferRef();
  if (Buf->getBuffer().starts_with("DXBC")) {
    ExitOnError ExitOnErr(Twine(argv[0]) + ": " + InputFilename + ": ");
    auto Container = ExitOnErr(object::DXContainer::create(BitcodeData));
    const auto &DXIL = Container.getDXIL(/*Debug=*/false);
    if (!DXIL) {
      errs() << argv[0] << ": " << InputFilename
             << ": container does not contain a DXIL part\n";
      return 1;
    }
    BitcodeData =
        MemoryBufferRef(StringRef(DXIL->second, DXIL->first.Bitcode.Size),
                        Buf->getBufferIdentifier());
  }

  ErrorOr<std::unique_ptr<Module>> MOrErr =
      dxil::parseBitcodeFile(BitcodeData, Context);
  if (std::error_code EC = MOrErr.getError()) {
    errs() << argv[0] << ": "
           << "Could not load bitcode from file '" << InputFilename
           << "': " << EC.message() << '\n';
    return 1;
  }
  M = std::move(*MOrErr);
  // Just use stdout.  We won't actually print anything on it.
  if (DontPrint)
    OutputFilename = "-";

  if (OutputFilename.empty()) { // Unspecified output, infer it.
    if (InputFilename == "-") {
      OutputFilename = "-";
    } else {
      StringRef IFN = InputFilename;
      OutputFilename = (IFN.ends_with(".bc") ? IFN.drop_back(3) : IFN).str();
      OutputFilename += ".ll";
    }
  }

  std::error_code EC;
  ToolOutputFile Out(OutputFilename, EC, sys::fs::OF_Text);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  std::unique_ptr<AssemblyAnnotationWriter> Annotator;
  if (ShowAnnotations)
    Annotator.reset(new CommentWriter());

  // All that llvm-dis does is write the assembly to a file.
  if (!DontPrint)
    M->print(Out.os(), Annotator.get(), PreserveAssemblyUseListOrder);

  // Declare success.
  Out.keep();

  return 0;
}
