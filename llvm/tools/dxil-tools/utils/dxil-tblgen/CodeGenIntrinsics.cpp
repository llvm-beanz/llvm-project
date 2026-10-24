//===- CodeGenIntrinsics.cpp - Intrinsic Class Wrapper
//----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Extracted from DXC's utils/TableGen/CodeGenTarget.cpp.
//
//===----------------------------------------------------------------------===//

#include "CodeGenIntrinsics.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
using namespace llvm;

namespace dxil {

MVT::SimpleValueType getValueType(const Record *Rec) {
  return static_cast<MVT::SimpleValueType>(Rec->getValueAsInt("Value"));
}

std::vector<CodeGenIntrinsic> LoadIntrinsics(const RecordKeeper &RC) {
  auto Definitions = llvm::to_vector(RC.getAllDerivedDefinitions("Intrinsic"));
  // Modern TableGen uses natural ordering; preserve DXC's intrinsic IDs.
  llvm::sort(Definitions, [](const Record *L, const Record *R) {
    return L->getName() < R->getName();
  });
  std::vector<CodeGenIntrinsic> Result;
  for (const Record *R : Definitions)
    if (!R->getValueAsBit("isTarget"))
      Result.emplace_back(R);
  return Result;
}

CodeGenIntrinsic::CodeGenIntrinsic(const Record *R) {
  TheDef = R;
  std::string DefName = R->getName().str();
  ModRef = ReadWriteMem;
  isOverloaded = false;
  isCommutative = false;
  canThrow = false;
  isNoReturn = false;
  isNoDuplicate = false;
  isConvergent = false;

  if (!R->getName().starts_with("int_") || DefName.size() <= 4)
    PrintFatalError(R,
                    "Intrinsic '" + DefName + "' does not start with 'int_'!");
  EnumName = DefName.substr(4);

  Name = R->getValueAsString("LLVMName");

  if (Name.empty()) {
    Name = "llvm." + EnumName;
    std::replace(Name.begin(), Name.end(), '_', '.');
  } else if (!StringRef(Name).starts_with("llvm.") || Name.size() <= 5) {
    PrintFatalError(R, "Intrinsic '" + DefName +
                           "'s name does not start with 'llvm.'!");
  }

  std::vector<MVT::SimpleValueType> OverloadedVTs;
  const ListInit *TypeList = R->getValueAsListInit("RetTypes");
  for (unsigned I = 0, E = TypeList->size(); I != E; ++I) {
    const Record *TyEl = TypeList->getElementAsRecord(I);
    assert(TyEl->isSubClassOf("LLVMType") && "Expected a type!");
    MVT::SimpleValueType VT;
    if (TyEl->isSubClassOf("LLVMMatchType")) {
      unsigned MatchTy = TyEl->getValueAsInt("Number");
      if (MatchTy >= OverloadedVTs.size())
        PrintFatalError(TyEl, "Invalid matching number!");
      VT = OverloadedVTs[MatchTy];
      assert(((!TyEl->isSubClassOf("LLVMExtendedType") &&
               !TyEl->isSubClassOf("LLVMTruncatedType")) ||
              VT == MVT::iAny || VT == MVT::vAny) &&
             "Expected iAny or vAny type");
    } else {
      VT = getValueType(TyEl->getValueAsDef("VT"));
    }
    if (MVT(VT).isOverloaded()) {
      OverloadedVTs.push_back(VT);
      isOverloaded = true;
    }
    if (VT == MVT::isVoid)
      PrintFatalError(R, "Intrinsic '" + DefName +
                             "' has void in result type list!");
    IS.RetVTs.push_back(VT);
    IS.RetTypeDefs.push_back(TyEl);
  }

  TypeList = R->getValueAsListInit("ParamTypes");
  for (unsigned I = 0, E = TypeList->size(); I != E; ++I) {
    const Record *TyEl = TypeList->getElementAsRecord(I);
    assert(TyEl->isSubClassOf("LLVMType") && "Expected a type!");
    MVT::SimpleValueType VT;
    if (TyEl->isSubClassOf("LLVMMatchType")) {
      unsigned MatchTy = TyEl->getValueAsInt("Number");
      if (MatchTy >= OverloadedVTs.size())
        PrintFatalError(TyEl, "Invalid matching number!");
      VT = OverloadedVTs[MatchTy];
      assert(((!TyEl->isSubClassOf("LLVMExtendedType") &&
               !TyEl->isSubClassOf("LLVMTruncatedType") &&
               !TyEl->isSubClassOf("LLVMVectorSameWidth") &&
               !TyEl->isSubClassOf("LLVMPointerToElt")) ||
              VT == MVT::iAny || VT == MVT::vAny) &&
             "Expected iAny or vAny type");
    } else {
      VT = getValueType(TyEl->getValueAsDef("VT"));
    }
    if (MVT(VT).isOverloaded()) {
      OverloadedVTs.push_back(VT);
      isOverloaded = true;
    }
    if (VT == MVT::isVoid && I != E - 1)
      PrintFatalError(R, "Intrinsic '" + DefName +
                             "' has void in parameter type list!");
    IS.ParamVTs.push_back(VT);
    IS.ParamTypeDefs.push_back(TyEl);
  }

  const ListInit *PropList = R->getValueAsListInit("Properties");
  for (unsigned I = 0, E = PropList->size(); I != E; ++I) {
    const Record *Property = PropList->getElementAsRecord(I);
    assert(Property->isSubClassOf("IntrinsicProperty") &&
           "Expected a property!");
    if (Property->getName() == "IntrNoMem")
      ModRef = NoMem;
    else if (Property->getName() == "IntrReadArgMem")
      ModRef = ReadArgMem;
    else if (Property->getName() == "IntrReadMem")
      ModRef = ReadMem;
    else if (Property->getName() == "IntrReadWriteArgMem")
      ModRef = ReadWriteArgMem;
    else if (Property->getName() == "Commutative")
      isCommutative = true;
    else if (Property->getName() == "Throws")
      canThrow = true;
    else if (Property->getName() == "IntrNoDuplicate")
      isNoDuplicate = true;
    else if (Property->getName() == "IntrConvergent")
      isConvergent = true;
    else if (Property->getName() == "IntrNoReturn")
      isNoReturn = true;
    else if (Property->isSubClassOf("NoCapture"))
      ArgumentAttributes.emplace_back(Property->getValueAsInt("ArgNo"),
                                      NoCapture);
    else if (Property->isSubClassOf("ReadOnly"))
      ArgumentAttributes.emplace_back(Property->getValueAsInt("ArgNo"),
                                      ReadOnly);
    else if (Property->isSubClassOf("ReadNone"))
      ArgumentAttributes.emplace_back(Property->getValueAsInt("ArgNo"),
                                      ReadNone);
    else
      PrintFatalError(Property, "Unknown intrinsic property");
  }
  std::sort(ArgumentAttributes.begin(), ArgumentAttributes.end());
}

} // namespace dxil
