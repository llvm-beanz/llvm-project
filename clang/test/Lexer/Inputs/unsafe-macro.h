// expected-error@+1{{expected (}}
#pragma clang header_unsafe

// expected-error@+1{{expected identifier}}
#pragma clang header_unsafe(4

// expected-error@+1{{no macro named foo}}
#pragma clang header_unsafe(foo)


#define UNSAFE_MACRO 1
#pragma clang header_unsafe(UNSAFE_MACRO, "Don't use this!")

#define UNSAFE_MACRO_2 2
#pragma clang header_unsafe(UNSAFE_MACRO_2)

// expected-error@+1{{expected )}}
#pragma clang deprecated(UNSAFE_MACRO
