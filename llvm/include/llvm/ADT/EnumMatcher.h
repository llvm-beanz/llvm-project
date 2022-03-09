#ifndef LLVM_ADT_ENUMMATCHER_H
#define LLVM_ADT_ENUMMATCHER_H

#include "llvm/Support/Compiler.h"

namespace llvm {

namespace detail {
// This magic blob of code here looks awful, but it is actually pretty clever.
// If you call isValidForDXIL with a constant value, this should generate an
// insane AST that will all constant fold down to true or false.
// If you call it with a non-constant value, it should turn into a nasty switch,
// which is as good as you're going to do anyways...
template <typename T>
LLVM_ATTRIBUTE_ALWAYS_INLINE bool unrollCompare(T Left, T Right) {
  return Left == Right;
}

template <typename T, typename... V>
LLVM_ATTRIBUTE_ALWAYS_INLINE bool unrollCompare(T Left, T Right, V... Vals) {
  if (Left == Right)
    return true;
  return unrollCompare(Left, Vals...);
}
} // namespace detail

// TODO: When we update to C++17 we can use auto here to avoid needing to
// specify the type of the enum.
template <typename T, T Val, T... Vals>
LLVM_ATTRIBUTE_ALWAYS_INLINE bool isInEnumSet(T InVal) {
  return detail::unrollCompare(InVal, Val, Vals...);
}

} // namespace llvm

#endif // LLVM_ADT_ENUMMATCHER_H
