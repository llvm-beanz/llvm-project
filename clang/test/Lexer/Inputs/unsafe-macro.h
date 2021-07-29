// expected-error@+1{{expected (}}
#pragma clang header_unsafe

// expected-error@+1{{expected identifier}}
#pragma clang header_unsafe(4

// expected-error@+1{{no macro named foo}}
#pragma clang header_unsafe(foo)


#define UNSAFE_MACRO 1
// expected-note@+8{{macro marked 'header_unsafe' here}}
// expected-note@+7{{macro marked 'header_unsafe' here}}
// expected-note@+6{{macro marked 'header_unsafe' here}}
// expected-note@+5{{macro marked 'header_unsafe' here}}
// expected-note@+4{{macro marked 'header_unsafe' here}}
// expected-note@+3{{macro marked 'header_unsafe' here}}
// expected-note@+2{{macro marked 'header_unsafe' here}}
// expected-note@+1{{macro marked 'header_unsafe' here}} 
#pragma clang header_unsafe(UNSAFE_MACRO, "Don't use this!")

#define UNSAFE_MACRO_2 2
// expected-note@+1{{macro marked 'header_unsafe' here}}
#pragma clang header_unsafe(UNSAFE_MACRO_2)

// expected-error@+1{{expected )}}
#pragma clang deprecated(UNSAFE_MACRO
