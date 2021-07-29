#define UNSAFE_MACRO 1
#pragma clang header_unsafe(UNSAFE_MACRO, "Don't use this!")
// not-expected-warning@+1{{macro 'UNSAFE_MACRO' has been marked as unsafe for use in headers: Don't use this!}}
#pragma clang deprecated(UNSAFE_MACRO, "Don't use this!")

#define UNSAFE_MACRO_2 1
#pragma clang deprecated(UNSAFE_MACRO_2, "Don't use this!")
// not-expected-warning@+1{{macro 'UNSAFE_MACRO_2' has been marked as deprecated: Don't use this!}}
#pragma clang header_unsafe(UNSAFE_MACRO_2, "Don't use this!")
