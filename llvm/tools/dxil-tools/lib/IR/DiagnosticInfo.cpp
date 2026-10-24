//===- llvm/Support/DiagnosticInfo.cpp - Diagnostic Definitions -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the different classes involved in low level diagnostics.
//
// Diagnostics reporting is still done as part of the LLVMContext.
//===----------------------------------------------------------------------===//

#include "dxil/IR/DiagnosticInfo.h"
#include "LLVMContextImpl.h"
#include "dxil/IR/Constants.h"
#include "dxil/IR/DebugInfoMetadata.h"
#include "dxil/IR/DiagnosticPrinter.h"
#include "dxil/IR/Function.h"
#include "dxil/IR/Instruction.h"
#include "dxil/IR/Metadata.h"
#include "dxil/IR/Module.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DiagnosticHandler.h"
#include <atomic>
#include <string>

using namespace llvm;

namespace dxil {

int getNextAvailablePluginDiagnosticKind() {
  static std::atomic<int> PluginKindID(DK_FirstPluginKind);
  return ++PluginKindID;
}

DiagnosticInfoInlineAsm::DiagnosticInfoInlineAsm(const Instruction &I,
                                                 const Twine &MsgStr,
                                                 DiagnosticSeverity Severity)
    : DiagnosticInfo(DK_InlineAsm, Severity), LocCookie(0), MsgStr(MsgStr),
      Instr(&I) {
  if (const MDNode *SrcLoc = I.getMetadata("srcloc")) {
    if (SrcLoc->getNumOperands() != 0)
      if (const auto *CI =
              mdconst::dyn_extract<ConstantInt>(SrcLoc->getOperand(0)))
        LocCookie = CI->getZExtValue();
  }
}

void DiagnosticInfoInlineAsm::print(DiagnosticPrinter &DP) const {
  DP << getMsgStr();
  if (getLocCookie())
    DP << " at line " << getLocCookie();
}

void DiagnosticInfoStackSize::print(DiagnosticPrinter &DP) const {
  DP << "stack size limit exceeded (" << getStackSize() << ") in "
     << getFunction();
}

void DiagnosticInfoDebugMetadataVersion::print(DiagnosticPrinter &DP) const {
  DP << "ignoring debug info with an invalid version (" << getMetadataVersion()
     << ") in " << getModule();
}

void DiagnosticInfoSampleProfile::print(DiagnosticPrinter &DP) const {
  if (getFileName() && getLineNum() > 0)
    DP << getFileName() << ":" << getLineNum() << ": ";
  else if (getFileName())
    DP << getFileName() << ": ";
  DP << getMsg();
}

bool DiagnosticInfoOptimizationBase::isLocationAvailable() const {
  return getDebugLoc();
}

void DiagnosticInfoOptimizationBase::getLocation(StringRef *Filename,
                                                 unsigned *Line,
                                                 unsigned *Column) const {
  DILocation *L = getDebugLoc();
  assert(L != nullptr && "debug location is invalid");
  *Filename = L->getFilename();
  *Line = L->getLine();
  *Column = L->getColumn();
}

const std::string DiagnosticInfoOptimizationBase::getLocationStr() const {
  StringRef Filename("<unknown>");
  unsigned Line = 0;
  unsigned Column = 0;
  if (isLocationAvailable())
    getLocation(&Filename, &Line, &Column);
  return (Filename + ":" + Twine(Line) + ":" + Twine(Column)).str();
}

void DiagnosticInfoOptimizationBase::print(DiagnosticPrinter &DP) const {
  DP << getLocationStr() << ": " << getMsg();
}

bool DiagnosticInfoOptimizationRemark::isEnabled() const {
  return llvm::DiagnosticHandler().isPassedOptRemarkEnabled(getPassName());
}

bool DiagnosticInfoOptimizationRemarkMissed::isEnabled() const {
  return llvm::DiagnosticHandler().isMissedOptRemarkEnabled(getPassName());
}

bool DiagnosticInfoOptimizationRemarkAnalysis::isEnabled() const {
  return llvm::DiagnosticHandler().isAnalysisRemarkEnabled(getPassName());
}

void DiagnosticInfoMIRParser::print(DiagnosticPrinter &DP) const {
  DP << Diagnostic;
}

void emitOptimizationRemark(LLVMContext &Ctx, const char *PassName,
                            const Function &Fn, const DebugLoc &DLoc,
                            const Twine &Msg) {
  Ctx.diagnose(DiagnosticInfoOptimizationRemark(PassName, Fn, DLoc, Msg));
}

void emitOptimizationRemarkMissed(LLVMContext &Ctx, const char *PassName,
                                  const Function &Fn, const DebugLoc &DLoc,
                                  const Twine &Msg) {
  Ctx.diagnose(DiagnosticInfoOptimizationRemarkMissed(PassName, Fn, DLoc, Msg));
}

void emitOptimizationRemarkAnalysis(LLVMContext &Ctx, const char *PassName,
                                    const Function &Fn, const DebugLoc &DLoc,
                                    const Twine &Msg) {
  Ctx.diagnose(
      DiagnosticInfoOptimizationRemarkAnalysis(PassName, Fn, DLoc, Msg));
}

bool DiagnosticInfoOptimizationFailure::isEnabled() const {
  // Only print warnings.
  return getSeverity() == DS_Warning;
}

void emitLoopVectorizeWarning(LLVMContext &Ctx, const Function &Fn,
                              const DebugLoc &DLoc, const Twine &Msg) {
  Ctx.diagnose(DiagnosticInfoOptimizationFailure(
      Fn, DLoc, Twine("loop not vectorized: " + Msg)));
}

void emitLoopInterleaveWarning(LLVMContext &Ctx, const Function &Fn,
                               const DebugLoc &DLoc, const Twine &Msg) {
  Ctx.diagnose(DiagnosticInfoOptimizationFailure(
      Fn, DLoc, Twine("loop not interleaved: " + Msg)));
}

// HLSL Change start - Dxil Diagnostic Info reporter
DiagnosticInfoDxil::DiagnosticInfoDxil(const Function *F, const DILocation *Loc, const Twine &MsgStr,
                                       DiagnosticSeverity Severity)
  : DiagnosticInfoDxil(F, MsgStr, Severity)
{
  if (Loc) {
    HasLocation = true;
    FileName = Loc->getFilename();
    Line     = Loc->getLine();
    Column   = Loc->getColumn();
  }
}

DiagnosticInfoDxil::DiagnosticInfoDxil(const Function *F, const DIGlobalVariable *DGV, const Twine &MsgStr,
                   DiagnosticSeverity Severity)
  : DiagnosticInfoDxil(F, MsgStr, Severity)
{
  if (DGV) {
    HasLocation = true;
    FileName = DGV->getFilename();
    Line     = DGV->getLine();
    Column   = 0;
  }
}

// Slapdash printing of diagnostic information as a last resort
// Used by validation and linker errors. Doesn't include source snippets.
void DiagnosticInfoDxil::print(DiagnosticPrinter &DP) const {
  if (HasLocation) {
    DP << FileName << ":" << Line << ":";
    if (Column > 0)
      DP << Column << ":";
    DP << " ";
  } else if (Func) {
    DP << "Function: " << Func->getName() << ": ";
  }

  switch (getSeverity()) {
  case DiagnosticSeverity::DS_Note:    DP << "note: "; break;
  case DiagnosticSeverity::DS_Remark:  DP << "remark: "; break;
  case DiagnosticSeverity::DS_Warning: DP << "warning: "; break;
  case DiagnosticSeverity::DS_Error:   DP << "error: "; break;
  }
  DP << getMsgStr();
}
// HLSL Change end - Dxil Diagnostic Info reporter

} // namespace dxil
