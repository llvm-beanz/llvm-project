// RUN: %clang_cc1 -Wpedantic-macros %s -fsyntax-only -verify

// Test warning production
// expected-note@+1{{previous definition is here}}
#define Foo 1
#pragma clang final(Foo)
#pragma clang deprecated(Foo)
#pragma clang header_unsafe(Foo)

// expected-warning@+1{{'Foo' macro redefined}}
#define Foo 2

// expected-warning@+1{{redefining builtin macro}}
#define __TIME__ 1

// expected-warning@+1{{undefining builtin macro}}
#undef __TIMESTAMP__

// expected-warning@+1{{macro 'Foo' has been marked as final and should not be undefined}}
#undef Foo
// expected-warning@+1{{macro 'Foo' has been marked as final and should not be redefined}}
#define Foo 3

// Test that unsafe_header and deprecated markings stick around after the undef
#include "Inputs/final-macro.h"

// expected-warning@+1{{macro 'Foo' has been marked as deprecated}}
const int X = Foo;

// Test parse errors
// expected-error@+1{{expected (}}
#pragma clang final

// expected-error@+1{{expected )}}
#pragma clang final(Foo

// expected-error@+1{{no macro named Baz}}
#pragma clang final(Baz)

// expected-error@+1{{expected identifier}}
#pragma clang final(4)

// expected-error@+1{{expected (}}
#pragma clang final Baz
