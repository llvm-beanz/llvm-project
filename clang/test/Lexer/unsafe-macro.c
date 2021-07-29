// RUN: %clang_cc1 -Wheader-unsafe-macro %s -fsyntax-only -verify
#include "Inputs/unsafe-macro.h"
#include "Inputs/unsafe-macro-2.h"

// not-expected-warning@+1{{macro 'UNSAFE_MACRO' has been marked as unsafe for use in headers: Don't use this!}}
#if UNSAFE_MACRO
#endif
