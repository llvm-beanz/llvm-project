//===- DebugInfo.h - Debug Information Helpers ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a bunch of datatypes that are useful for creating and
// walking debug info in LLVM IR form. They essentially provide wrappers around
// the information in the global variables that's needed when constructing the
// DWARF information.
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_IR_DEBUGINFO_H
#define DXIL_IR_DEBUGINFO_H

#include "dxil/IR/DebugInfoMetadata.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"

namespace dxil {
class Module;

/// \brief Maps from type identifier to the actual MDNode.
typedef llvm::DenseMap<const MDString *, DIType *> DITypeIdentifierMap;

/// \brief Find subprogram that is enclosing this scope.
DISubprogram *getDISubprogram(const MDNode *Scope);

/// \brief Find debug info for a given function.
///
/// \returns a valid subprogram, if found. Otherwise, return \c nullptr.
DISubprogram *getDISubprogram(const Function *F);

/// \brief Find underlying composite type.
DICompositeTypeBase *getDICompositeType(DIType *T);

/// \brief Generate map by visiting all retained types.
DITypeIdentifierMap generateDITypeIdentifierMap(const NamedMDNode *CU_Nodes);

/// \brief Strip debug info in the module if it exists.
///
/// To do this, we remove all calls to the debugger intrinsics and any named
/// metadata for debugging. We also remove debug locations for instructions.
/// Return true if module is modified.
bool StripDebugInfo(Module &M);
bool stripDebugInfo(Function &F);

/// \brief Return Debug Info Metadata Version by checking module flags.
unsigned getDebugMetadataVersionFromModule(const Module &M);
bool hasDebugInfo(const Module &M); // HLSL Change - Helper function to check if there's real debug info (variables, types)

llvm::DenseMap<const Function *, DISubprogram *>
makeSubprogramMap(const Module &M);

} // end namespace llvm

#endif
