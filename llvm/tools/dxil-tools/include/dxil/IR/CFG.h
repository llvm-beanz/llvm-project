//===- CFG.h - Process LLVM structures as graphs ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines specializations of GraphTraits that allow Function and
// BasicBlock graphs to be treated as proper graphs for generic algorithms.
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_IR_CFG_H
#define DXIL_IR_CFG_H

#include "dxil/IR/Function.h"
#include "dxil/IR/InstrTypes.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"

namespace dxil {

//===----------------------------------------------------------------------===//
// BasicBlock pred_iterator definition
//===----------------------------------------------------------------------===//

template <class Ptr, class USE_iterator> // Predecessor Iterator
class PredIterator {
public:
  // HLSL Change start: The iterator wasn't satisfying the std::forward_iterator
  // concept, which caused std::vector::insert calls to fail in C++23.
  using iterator_category = std::forward_iterator_tag;
  using value_type = Ptr *;
  using difference_type = std::ptrdiff_t;
  using pointer = Ptr **;
  // HLSL Change end
  using reference = Ptr *;

private:
  typedef PredIterator<Ptr, USE_iterator> Self;
  USE_iterator It;

  inline void advancePastNonTerminators() {
    // Loop to ignore non-terminator uses (for example BlockAddresses).
    while (!It.atEnd() && !llvm::isa<TerminatorInst>(*It))
      ++It;
  }

public:

  PredIterator() {}
  explicit inline PredIterator(Ptr *bb) : It(bb->user_begin()) {
    advancePastNonTerminators();
  }
  inline PredIterator(Ptr *bb, bool) : It(bb->user_end()) {}

  inline bool operator==(const Self& x) const { return It == x.It; }
  inline bool operator!=(const Self& x) const { return !operator==(x); }

  inline reference operator*() const {
    assert(!It.atEnd() && "pred_iterator out of range!");
    return cast<TerminatorInst>(*It)->getParent();
  }
  inline pointer *operator->() const { return &operator*(); }

  inline Self& operator++() {   // Preincrement
    assert(!It.atEnd() && "pred_iterator out of range!");
    ++It; advancePastNonTerminators();
    return *this;
  }

  inline Self operator++(int) { // Postincrement
    Self tmp = *this; ++*this; return tmp;
  }

  /// getOperandNo - Return the operand number in the predecessor's
  /// terminator of the successor.
  unsigned getOperandNo() const {
    return It.getOperandNo();
  }

  /// getUse - Return the operand Use in the predecessor's terminator
  /// of the successor.
  Use &getUse() const {
    return It.getUse();
  }
};

typedef PredIterator<BasicBlock, Value::user_iterator> pred_iterator;
typedef PredIterator<const BasicBlock,
                     Value::const_user_iterator> const_pred_iterator;
typedef llvm::iterator_range<pred_iterator> pred_range;
typedef llvm::iterator_range<const_pred_iterator> pred_const_range;

inline pred_iterator pred_begin(BasicBlock *BB) { return pred_iterator(BB); }
inline const_pred_iterator pred_begin(const BasicBlock *BB) {
  return const_pred_iterator(BB);
}
inline pred_iterator pred_end(BasicBlock *BB) { return pred_iterator(BB, true);}
inline const_pred_iterator pred_end(const BasicBlock *BB) {
  return const_pred_iterator(BB, true);
}
inline bool pred_empty(const BasicBlock *BB) {
  return pred_begin(BB) == pred_end(BB);
}
inline pred_range predecessors(BasicBlock *BB) {
  return pred_range(pred_begin(BB), pred_end(BB));
}
inline pred_const_range predecessors(const BasicBlock *BB) {
  return pred_const_range(pred_begin(BB), pred_end(BB));
}

//===----------------------------------------------------------------------===//
// BasicBlock succ_iterator definition
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

template <class Term_, class BB_>           // Successor Iterator
class SuccIterator {

public:
  using iterator_category = std::random_access_iterator_tag;
  // HLSL Change start: The iterator wasn't satisfying the std::forward_iterator
  // concept, which caused std::vector::insert calls to fail in C++23.
  using value_type = BB_ *;
  using difference_type = std::ptrdiff_t;
  // HLSL Change end
  using pointer = BB_ *;
  using reference = BB_ *;

private:
  Term_ Term;
  unsigned idx;
  typedef SuccIterator<Term_, BB_> Self;

  inline bool index_is_valid(int idx) {
    // HLSL Change Begin
    return idx >= 0 && (unsigned)idx <= Term->getNumSuccessors();
    // HLSL Change End
  }

  /// \brief Proxy object to allow write access in operator[]
  class SuccessorProxy {
    Self it;

  public:
    explicit SuccessorProxy(const Self &it) : it(it) {}

    SuccessorProxy(const SuccessorProxy&) = default;

    SuccessorProxy &operator=(SuccessorProxy r) {
      *this = reference(r);
      return *this;
    }

    SuccessorProxy &operator=(reference r) {
      it.Term->setSuccessor(it.idx, r);
      return *this;
    }

    operator reference() const { return *it; }
  };

public:
  explicit inline SuccIterator(Term_ T) : Term(T), idx(0) {// begin iterator
  }
  inline SuccIterator(Term_ T, bool)                       // end iterator
    : Term(T) {
    if (Term)
      idx = Term->getNumSuccessors();
    else
      // Term == NULL happens, if a basic block is not fully constructed and
      // consequently getTerminator() returns NULL. In this case we construct a
      // SuccIterator which describes a basic block that has zero successors.
      // Defining SuccIterator for incomplete and malformed CFGs is especially
      // useful for debugging.
      idx = 0;
  }

  /// getSuccessorIndex - This is used to interface between code that wants to
  /// operate on terminator instructions directly.
  unsigned getSuccessorIndex() const { return idx; }

  inline bool operator==(const Self& x) const { return idx == x.idx; }
  inline bool operator!=(const Self& x) const { return !operator==(x); }

  inline reference operator*() const { return Term->getSuccessor(idx); }
  inline pointer operator->() const { return operator*(); }

  inline Self& operator++() { ++idx; return *this; } // Preincrement

  inline Self operator++(int) { // Postincrement
    Self tmp = *this; ++*this; return tmp;
  }

  inline Self& operator--() { --idx; return *this; }  // Predecrement
  inline Self operator--(int) { // Postdecrement
    Self tmp = *this; --*this; return tmp;
  }

  inline bool operator<(const Self& x) const {
    assert(Term == x.Term && "Cannot compare iterators of different blocks!");
    return idx < x.idx;
  }

  inline bool operator<=(const Self& x) const {
    assert(Term == x.Term && "Cannot compare iterators of different blocks!");
    return idx <= x.idx;
  }
  inline bool operator>=(const Self& x) const {
    assert(Term == x.Term && "Cannot compare iterators of different blocks!");
    return idx >= x.idx;
  }

  inline bool operator>(const Self& x) const {
    assert(Term == x.Term && "Cannot compare iterators of different blocks!");
    return idx > x.idx;
  }

  inline Self& operator+=(int Right) {
    unsigned new_idx = idx + Right;
    assert(index_is_valid(new_idx) && "Iterator index out of bound");
    idx = new_idx;
    return *this;
  }

  inline Self operator+(int Right) const {
    Self tmp = *this;
    tmp += Right;
    return tmp;
  }

  inline Self& operator-=(int Right) {
    return operator+=(-Right);
  }

  inline Self operator-(int Right) const {
    return operator+(-Right);
  }

  inline int operator-(const Self& x) const {
    assert(Term == x.Term && "Cannot work on iterators of different blocks!");
    int distance = idx - x.idx;
    return distance;
  }

  inline SuccessorProxy operator[](int offset) {
   Self tmp = *this;
   tmp += offset;
   return SuccessorProxy(tmp);
  }

  /// Get the source BB of this iterator.
  inline BB_ *getSource() {
    assert(Term && "Source not available, if basic block was malformed");
    return Term->getParent();
  }
};

typedef SuccIterator<TerminatorInst*, BasicBlock> succ_iterator;
typedef SuccIterator<const TerminatorInst*,
                     const BasicBlock> succ_const_iterator;
typedef llvm::iterator_range<succ_iterator> succ_range;
typedef llvm::iterator_range<succ_const_iterator> succ_const_range;

inline succ_iterator succ_begin(BasicBlock *BB) {
  return succ_iterator(BB->getTerminator());
}
inline succ_const_iterator succ_begin(const BasicBlock *BB) {
  return succ_const_iterator(BB->getTerminator());
}
inline succ_iterator succ_end(BasicBlock *BB) {
  return succ_iterator(BB->getTerminator(), true);
}
inline succ_const_iterator succ_end(const BasicBlock *BB) {
  return succ_const_iterator(BB->getTerminator(), true);
}
inline bool succ_empty(const BasicBlock *BB) {
  return succ_begin(BB) == succ_end(BB);
}
inline succ_range successors(BasicBlock *BB) {
  return succ_range(succ_begin(BB), succ_end(BB));
}
inline succ_const_range successors(const BasicBlock *BB) {
  return succ_const_range(succ_begin(BB), succ_end(BB));
}

} // namespace dxil

namespace llvm {

//===--------------------------------------------------------------------===//
// GraphTraits specializations for basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks...

template <> struct GraphTraits<dxil::BasicBlock *> {
  using NodeRef = dxil::BasicBlock *;
  typedef dxil::succ_iterator ChildIteratorType;

  static NodeRef getEntryNode(dxil::BasicBlock *BB) { return BB; }
  static inline ChildIteratorType child_begin(NodeRef N) {
    return succ_begin(N);
  }
  static inline ChildIteratorType child_end(NodeRef N) { return succ_end(N); }
};

template <> struct GraphTraits<const dxil::BasicBlock *> {
  using NodeRef = const dxil::BasicBlock *;
  typedef dxil::succ_const_iterator ChildIteratorType;

  static NodeRef getEntryNode(const dxil::BasicBlock *BB) { return BB; }

  static inline ChildIteratorType child_begin(NodeRef N) {
    return succ_begin(N);
  }
  static inline ChildIteratorType child_end(NodeRef N) { return succ_end(N); }
};

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... and to walk it in inverse order.  Inverse order for
// a function is considered to be when traversing the predecessor edges of a BB
// instead of the successor edges.
//
template <> struct GraphTraits<Inverse<dxil::BasicBlock *>> {
  using NodeRef = dxil::BasicBlock *;
  typedef dxil::pred_iterator ChildIteratorType;
  static NodeRef getEntryNode(Inverse<dxil::BasicBlock *> G) { return G.Graph; }
  static inline ChildIteratorType child_begin(NodeRef N) {
    return pred_begin(N);
  }
  static inline ChildIteratorType child_end(NodeRef N) { return pred_end(N); }
};

template <> struct GraphTraits<Inverse<const dxil::BasicBlock *>> {
  using NodeRef = const dxil::BasicBlock *;
  typedef dxil::const_pred_iterator ChildIteratorType;
  static NodeRef getEntryNode(Inverse<const dxil::BasicBlock *> G) {
    return G.Graph;
  }
  static inline ChildIteratorType child_begin(NodeRef N) {
    return pred_begin(N);
  }
  static inline ChildIteratorType child_end(NodeRef N) { return pred_end(N); }
};

//===--------------------------------------------------------------------===//
// GraphTraits specializations for function basic block graphs (CFGs)
//===--------------------------------------------------------------------===//

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... these are the same as the basic block iterators,
// except that the root node is implicitly the first node of the function.
//
template <>
struct GraphTraits<dxil::Function *> : public GraphTraits<dxil::BasicBlock *> {
  static NodeRef getEntryNode(dxil::Function *F) { return &F->getEntryBlock(); }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<dxil::Function::iterator>;
  static nodes_iterator nodes_begin(dxil::Function *F) {
    return nodes_iterator(F->begin());
  }
  static nodes_iterator nodes_end(dxil::Function *F) {
    return nodes_iterator(F->end());
  }
  static size_t size(dxil::Function *F) { return F->size(); }
};
template <>
struct GraphTraits<const dxil::Function *>
    : public GraphTraits<const dxil::BasicBlock *> {
  static NodeRef getEntryNode(const dxil::Function *F) {
    return &F->getEntryBlock();
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<dxil::Function::const_iterator>;
  static nodes_iterator nodes_begin(const dxil::Function *F) {
    return nodes_iterator(F->begin());
  }
  static nodes_iterator nodes_end(const dxil::Function *F) {
    return nodes_iterator(F->end());
  }
  static size_t size(const dxil::Function *F) { return F->size(); }
};

// Provide specializations of GraphTraits to be able to treat a function as a
// graph of basic blocks... and to walk it in inverse order.  Inverse order for
// a function is considered to be when traversing the predecessor edges of a BB
// instead of the successor edges.
//
template <>
struct GraphTraits<Inverse<dxil::Function *>>
    : public GraphTraits<Inverse<dxil::BasicBlock *>> {
  static NodeRef getEntryNode(Inverse<dxil::Function *> G) {
    return &G.Graph->getEntryBlock();
  }
};
template <>
struct GraphTraits<Inverse<const dxil::Function *>>
    : public GraphTraits<Inverse<const dxil::BasicBlock *>> {
  static NodeRef getEntryNode(Inverse<const dxil::Function *> G) {
    return &G.Graph->getEntryBlock();
  }
};

} // namespace llvm

#endif
