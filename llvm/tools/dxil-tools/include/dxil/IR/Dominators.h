//===- Dominators.h - Dominator Info Calculation ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the DominatorTree class, which provides fast and efficient
// dominance queries.
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_IR_DOMINATORS_H
#define DXIL_IR_DOMINATORS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "dxil/IR/BasicBlock.h"
#include "dxil/IR/CFG.h"
#include "dxil/IR/Function.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/GenericDomTree.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

namespace dxil {

using DomTreeNode = llvm::DomTreeNodeBase<BasicBlock>;

class BasicBlockEdge {
  const BasicBlock *Start;
  const BasicBlock *End;
public:
  BasicBlockEdge(const BasicBlock *Start_, const BasicBlock *End_) :
    Start(Start_), End(End_) { }
  const BasicBlock *getStart() const {
    return Start;
  }
  const BasicBlock *getEnd() const {
    return End;
  }
  bool isSingleEdge() const;
};

/// \brief Concrete subclass of DominatorTreeBase that is used to compute a
/// normal dominator tree.
class DominatorTree : public llvm::DominatorTreeBase<BasicBlock, false> {
public:
  typedef llvm::DominatorTreeBase<BasicBlock, false> Base;

  DominatorTree() = default;

  DominatorTree(DominatorTree &&Arg)
      : Base(std::move(static_cast<Base &>(Arg))) {}
  DominatorTree &operator=(DominatorTree &&RHS) {
    Base::operator=(std::move(static_cast<Base &>(RHS)));
    return *this;
  }

  /// \brief Returns *false* if the other dominator tree matches this dominator
  /// tree.
  inline bool compare(const DominatorTree &Other) const {
    const DomTreeNode *R = getRootNode();
    const DomTreeNode *OtherR = Other.getRootNode();

    if (!R || !OtherR || R->getBlock() != OtherR->getBlock())
      return true;

    if (Base::compare(Other))
      return true;

    return false;
  }

  // Ensure base-class overloads are visible.
  using Base::dominates;

  /// \brief Return true if Def dominates a use in User.
  ///
  /// This performs the special checks necessary if Def and User are in the same
  /// basic block. Note that Def doesn't dominate a use in Def itself!
  bool dominates(const Instruction *Def, const Use &U) const;
  bool dominates(const Instruction *Def, const Instruction *User) const;
  bool dominates(const Instruction *Def, const BasicBlock *BB) const;
  bool dominates(const BasicBlockEdge &BBE, const Use &U) const;
  bool dominates(const BasicBlockEdge &BBE, const BasicBlock *BB) const;

  // Ensure base class overloads are visible.
  using Base::isReachableFromEntry;

  /// \brief Provide an overload for a Use.
  bool isReachableFromEntry(const Use &U) const;

  /// \brief Verify the correctness of the domtree by re-computing it.
  ///
  /// This should only be used for debugging as it aborts the program if the
  /// verification fails.
  void verifyDomTree() const;
};

} // namespace dxil

#endif
