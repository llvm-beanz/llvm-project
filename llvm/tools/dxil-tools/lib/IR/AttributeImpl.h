//===-- AttributeImpl.h - Attribute Internals -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file defines various helper methods and classes used by
/// LLVMContextImpl for creating and managing attributes.
///
//===----------------------------------------------------------------------===//

#ifndef DXIL_LIB_IR_ATTRIBUTEIMPL_H
#define DXIL_LIB_IR_ATTRIBUTEIMPL_H

#include "dxil/IR/Attributes.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include <string>

namespace dxil {

class Constant;
class LLVMContext;

//===----------------------------------------------------------------------===//
/// \class
/// \brief This class represents a single, uniqued attribute. That attribute
/// could be a single enum, a tuple, or a string.
class AttributeImpl : public llvm::FoldingSetNode {
  unsigned char KindID; ///< Holds the AttrEntryKind of the attribute

  // AttributesImpl is uniqued, these should not be publicly available.
  void operator=(const AttributeImpl &) = delete;
  AttributeImpl(const AttributeImpl &) = delete;

protected:
  enum AttrEntryKind { EnumAttrEntry, IntAttrEntry, StringAttrEntry };

  AttributeImpl(AttrEntryKind KindID) : KindID(KindID) {}

public:
  virtual ~AttributeImpl();

  bool isEnumAttribute() const { return KindID == EnumAttrEntry; }
  bool isIntAttribute() const { return KindID == IntAttrEntry; }
  bool isStringAttribute() const { return KindID == StringAttrEntry; }

  bool hasAttribute(Attribute::AttrKind A) const;
  bool hasAttribute(llvm::StringRef Kind) const;

  Attribute::AttrKind getKindAsEnum() const;
  uint64_t getValueAsInt() const;

  llvm::StringRef getKindAsString() const;
  llvm::StringRef getValueAsString() const;

  /// \brief Used when sorting the attributes.
  bool operator<(const AttributeImpl &AI) const;

  void Profile(llvm::FoldingSetNodeID &ID) const {
    if (isEnumAttribute())
      Profile(ID, getKindAsEnum(), 0);
    else if (isIntAttribute())
      Profile(ID, getKindAsEnum(), getValueAsInt());
    else
      Profile(ID, getKindAsString(), getValueAsString());
  }
  static void Profile(llvm::FoldingSetNodeID &ID, Attribute::AttrKind Kind,
                      uint64_t Val) {
    ID.AddInteger(Kind);
    if (Val)
      ID.AddInteger(Val);
  }
  static void Profile(llvm::FoldingSetNodeID &ID, llvm::StringRef Kind,
                      llvm::StringRef Values) {
    ID.AddString(Kind);
    if (!Values.empty())
      ID.AddString(Values);
  }

  // FIXME: Remove this!
  static uint64_t getAttrMask(Attribute::AttrKind Val);
};

//===----------------------------------------------------------------------===//
/// \class
/// \brief A set of classes that contain the value of the
/// attribute object. There are three main categories: enum attribute entries,
/// represented by Attribute::AttrKind; alignment attribute entries; and string
/// attribute enties, which are for target-dependent attributes.

class EnumAttributeImpl : public AttributeImpl {
  virtual void anchor();
  Attribute::AttrKind Kind;

protected:
  EnumAttributeImpl(AttrEntryKind ID, Attribute::AttrKind Kind)
      : AttributeImpl(ID), Kind(Kind) {}

public:
  EnumAttributeImpl(Attribute::AttrKind Kind)
      : AttributeImpl(EnumAttrEntry), Kind(Kind) {}

  Attribute::AttrKind getEnumKind() const { return Kind; }
};

class IntAttributeImpl : public EnumAttributeImpl {
  void anchor() override;
  uint64_t Val;

public:
  IntAttributeImpl(Attribute::AttrKind Kind, uint64_t Val)
      : EnumAttributeImpl(IntAttrEntry, Kind), Val(Val) {
    assert((Kind == Attribute::Alignment || Kind == Attribute::StackAlignment ||
            Kind == Attribute::Dereferenceable ||
            Kind == Attribute::DereferenceableOrNull) &&
           "Wrong kind for int attribute!");
  }

  uint64_t getValue() const { return Val; }
};

class StringAttributeImpl : public AttributeImpl {
  virtual void anchor();
  std::string Kind;
  std::string Val;

public:
  StringAttributeImpl(llvm::StringRef Kind, llvm::StringRef Val = {})
      : AttributeImpl(StringAttrEntry), Kind(Kind), Val(Val) {}

  llvm::StringRef getStringKind() const { return Kind; }
  llvm::StringRef getStringValue() const { return Val; }
};

//===----------------------------------------------------------------------===//
/// \class
/// \brief This class represents a group of attributes that apply to one
/// element: function, return type, or parameter.
class alignas(Attribute) AttributeSetNode : public llvm::FoldingSetNode {
  unsigned NumAttrs; ///< Number of attributes in this node.

  AttributeSetNode(llvm::ArrayRef<Attribute> Attrs) : NumAttrs(Attrs.size()) {
    // There's memory after the node where we can store the entries in.
    std::copy(Attrs.begin(), Attrs.end(),
              reinterpret_cast<Attribute *>(this + 1));
  }

  // AttributesSetNode is uniqued, these should not be publicly available.
  void operator=(const AttributeSetNode &) = delete;
  AttributeSetNode(const AttributeSetNode &) = delete;

public:
  static AttributeSetNode *get(LLVMContext &C, llvm::ArrayRef<Attribute> Attrs);

  bool hasAttribute(Attribute::AttrKind Kind) const;
  bool hasAttribute(llvm::StringRef Kind) const;
  bool hasAttributes() const { return NumAttrs != 0; }

  Attribute getAttribute(Attribute::AttrKind Kind) const;
  Attribute getAttribute(llvm::StringRef Kind) const;

  unsigned getAlignment() const;
  unsigned getStackAlignment() const;
  uint64_t getDereferenceableBytes() const;
  uint64_t getDereferenceableOrNullBytes() const;
  std::string getAsString(bool InAttrGrp) const;

  typedef const Attribute *iterator;
  iterator begin() const { return reinterpret_cast<iterator>(this + 1); }
  iterator end() const { return begin() + NumAttrs; }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, llvm::ArrayRef(begin(), end()));
  }
  static void Profile(llvm::FoldingSetNodeID &ID,
                      llvm::ArrayRef<Attribute> AttrList) {
    for (unsigned I = 0, E = AttrList.size(); I != E; ++I)
      AttrList[I].Profile(ID);
  }
};
static_assert(
    alignof(AttributeSetNode) >= alignof(Attribute),
    "Alignment is insufficient for objects appended to AttributeSetNode");
//                                                                           //
///////////////////////////////////////////////////////////////////////////////
/// \class
/// \brief This class represents a set of attributes that apply to the function,
/// return type, and parameters.
class AttributeSetImpl : public llvm::FoldingSetNode {
  friend class AttributeSet;

public:
  typedef std::pair<unsigned, AttributeSetNode *> IndexAttrPair;

private:
  LLVMContext &Context;
  unsigned NumAttrs; ///< Number of entries in this set.

  /// \brief Return a pointer to the IndexAttrPair for the specified slot.
  const IndexAttrPair *getNode(unsigned Slot) const {
    return reinterpret_cast<const IndexAttrPair *>(this + 1) + Slot;
  }

  // AttributesSet is uniqued, these should not be publicly available.
  void operator=(const AttributeSetImpl &) = delete;
  AttributeSetImpl(const AttributeSetImpl &) = delete;

public:
  AttributeSetImpl(
      LLVMContext &C,
      llvm::ArrayRef<std::pair<unsigned, AttributeSetNode *>> Attrs)
      : Context(C), NumAttrs(Attrs.size()) {

#ifndef NDEBUG
    if (Attrs.size() >= 2) {
      for (const std::pair<unsigned, AttributeSetNode *> *i = Attrs.begin() + 1,
                                                         *e = Attrs.end();
           i != e; ++i) {
        assert((i - 1)->first <= i->first && "Attribute set not ordered!");
      }
    }
#endif
    // There's memory after the node where we can store the entries in.
    std::copy(Attrs.begin(), Attrs.end(),
              reinterpret_cast<IndexAttrPair *>(this + 1));
  }

  /// \brief Get the context that created this AttributeSetImpl.
  LLVMContext &getContext() { return Context; }

  /// \brief Return the number of attributes this AttributeSet contains.
  unsigned getNumAttributes() const { return NumAttrs; }

  /// \brief Get the index of the given "slot" in the AttrNodes list. This index
  /// is the index of the return, parameter, or function object that the
  /// attributes are applied to, not the index into the AttrNodes list where the
  /// attributes reside.
  unsigned getSlotIndex(unsigned Slot) const { return getNode(Slot)->first; }

  /// \brief Retrieve the attributes for the given "slot" in the AttrNode list.
  /// \p Slot is an index into the AttrNodes list, not the index of the return /
  /// parameter/ function which the attributes apply to.
  AttributeSet getSlotAttributes(unsigned Slot) const {
    return AttributeSet::get(Context, *getNode(Slot));
  }

  /// \brief Retrieve the attribute set node for the given "slot" in the
  /// AttrNode list.
  AttributeSetNode *getSlotNode(unsigned Slot) const {
    return getNode(Slot)->second;
  }

  typedef AttributeSetNode::iterator iterator;
  iterator begin(unsigned Slot) const { return getSlotNode(Slot)->begin(); }
  iterator end(unsigned Slot) const { return getSlotNode(Slot)->end(); }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    Profile(ID, llvm::ArrayRef(getNode(0), getNumAttributes()));
  }
  static void
  Profile(llvm::FoldingSetNodeID &ID,
          llvm::ArrayRef<std::pair<unsigned, AttributeSetNode *>> Nodes) {
    for (unsigned i = 0, e = Nodes.size(); i != e; ++i) {
      ID.AddInteger(Nodes[i].first);
      ID.AddPointer(Nodes[i].second);
    }
  }

  // FIXME: This atrocity is temporary.
  uint64_t Raw(unsigned Index) const;

  void dump() const;
};
static_assert(
    alignof(AttributeSetImpl) >= alignof(AttributeSetImpl::IndexAttrPair),
    "Alignment is insufficient for objects appended to AttributeSetImpl");

} // namespace dxil

#endif
