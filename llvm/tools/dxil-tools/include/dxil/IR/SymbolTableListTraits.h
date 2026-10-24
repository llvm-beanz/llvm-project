//===-- llvm/SymbolTableListTraits.h - Traits for iplist --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines a generic class that is used to implement the automatic
// symbol table manipulation that occurs when you put (for example) a named
// instruction into a basic block.
//
// The way that this is implemented is by using a special traits class with the
// intrusive list that makes up the list of instructions in a basic block.  When
// a new element is added to the list of instructions, the traits class is
// notified, allowing the symbol table to be updated.
//
// This generic class implements the traits class.  It must be generic so that
// it can work for all uses it, which include lists of instructions, basic
// blocks, arguments, functions, global variables, etc...
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_IR_SYMBOLTABLELISTTRAITS_H
#define DXIL_IR_SYMBOLTABLELISTTRAITS_H

#include "llvm/ADT/ilist.h"

namespace dxil {
class ValueSymbolTable;

template <typename NodeTy, typename ParentTy> class SymbolTableList;

// ValueSubClass   - The type of objects that I hold, e.g. Instruction.
// ItemParentClass - The type of object that owns the list, e.g. BasicBlock.
//
template <typename ValueSubClass, typename ItemParentClass>
class SymbolTableListTraits : public llvm::ilist_alloc_traits<ValueSubClass> {
  using ListTy = SymbolTableList<ValueSubClass, ItemParentClass>;
  using iterator = typename llvm::simple_ilist<ValueSubClass>::iterator;

public:
  SymbolTableListTraits() {}

  /// getListOwner - Return the object that owns this list.  If this is a list
  /// of instructions, it returns the BasicBlock that owns them.
  ItemParentClass *getListOwner() {
    size_t Offset(size_t(&((ItemParentClass*)nullptr->*ItemParentClass::
                           getSublistAccess(static_cast<ValueSubClass*>(nullptr)))));
    ListTy *Anchor = static_cast<ListTy *>(this);
    return reinterpret_cast<ItemParentClass*>(reinterpret_cast<char*>(Anchor)-
                                              Offset);
  }

  static ListTy &getList(ItemParentClass *Par) {
    return Par->*(Par->getSublistAccess((ValueSubClass*)nullptr));
  }

  static ValueSymbolTable *getSymTab(ItemParentClass *Par) {
    return Par ? toPtr(Par->getValueSymbolTable()) : nullptr;
  }

  void addNodeToList(ValueSubClass *V);
  void removeNodeFromList(ValueSubClass *V);
  void transferNodesFromList(SymbolTableListTraits &L2, iterator first,
                             iterator last);
  // private:
  template<typename TPtr>
  void setSymTabObject(TPtr *, TPtr);
  static ValueSymbolTable *toPtr(ValueSymbolTable *P) { return P; }
  static ValueSymbolTable *toPtr(ValueSymbolTable &R) { return &R; }
};

template <typename NodeTy, typename ParentTy>
class SymbolTableList
    : public llvm::iplist_impl<llvm::simple_ilist<NodeTy>,
                               SymbolTableListTraits<NodeTy, ParentTy>> {};

} // End llvm namespace

#endif
