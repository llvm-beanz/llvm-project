//===- PatternMatch.h - Match on the LLVM IR --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides the scalar and vector constant matchers used by the
// legacy constant folder.
//
//===----------------------------------------------------------------------===//

#ifndef DXIL_IR_PATTERNMATCH_H
#define DXIL_IR_PATTERNMATCH_H

#include "dxil/IR/Constants.h"

namespace dxil {
namespace PatternMatch {

template <typename Val, typename Pattern> bool match(Val *V, const Pattern &P) {
  return const_cast<Pattern &>(P).match(V);
}

template <typename Class> struct class_match {
  template <typename ITy> bool match(ITy *V) { return llvm::isa<Class>(V); }
};

/// \brief Match an arbitrary undef constant.
inline class_match<UndefValue> m_Undef() { return class_match<UndefValue>(); }

struct match_zero {
  template <typename ITy> bool match(ITy *V) {
    if (const auto *C = llvm::dyn_cast<Constant>(V))
      return C->isNullValue();
    return false;
  }
};

/// \brief Match an arbitrary zero/null constant.  This includes
/// zero_initializer for vectors and ConstantPointerNull for pointers.
inline match_zero m_Zero() { return match_zero(); }

struct apint_match {
  const llvm::APInt *&Res;
  apint_match(const llvm::APInt *&R) : Res(R) {}
  template <typename ITy> bool match(ITy *V) {
    if (auto *CI = llvm::dyn_cast<ConstantInt>(V)) {
      Res = &CI->getValue();
      return true;
    }
    if (V->getType()->isVectorTy())
      if (const auto *C = llvm::dyn_cast<Constant>(V))
        if (auto *CI =
                llvm::dyn_cast_or_null<ConstantInt>(C->getSplatValue())) {
          Res = &CI->getValue();
          return true;
        }
    return false;
  }
};

/// \brief Match a ConstantInt or splatted ConstantVector, binding the
/// specified pointer to the contained llvm::APInt.
inline apint_match m_APInt(const llvm::APInt *&Res) { return Res; }

template <typename Predicate> struct cst_pred_ty : public Predicate {
  template <typename ITy> bool match(ITy *V) {
    if (const auto *CI = llvm::dyn_cast<ConstantInt>(V))
      return this->isValue(CI->getValue());
    if (V->getType()->isVectorTy())
      if (const auto *C = llvm::dyn_cast<Constant>(V))
        if (const auto *CI =
                llvm::dyn_cast_or_null<ConstantInt>(C->getSplatValue()))
          return this->isValue(CI->getValue());
    return false;
  }
};

struct is_one {
  bool isValue(const llvm::APInt &C) { return C == 1; }
};

/// \brief Match an integer 1 or a vector with all elements equal to 1.
inline cst_pred_ty<is_one> m_One() { return cst_pred_ty<is_one>(); }

} // namespace PatternMatch
} // namespace dxil

#endif
