// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{expected integer literal}}
dxsa.add r<0>, l(1.0), r<2>

// -----

// expected-error@+1 {{33-bit immediate does not fit in 32-bit literal}}
dxsa.add r<0>, l(0x100000000), r<2>

// -----

// expected-error@+1 {{37-bit immediate does not fit in 32-bit literal}}
dxsa.add r<0>, l(0x1FFFFFFFFF), r<2>

// -----

// expected-error@+1 {{65-bit immediate does not fit in 64-bit literal}}
dxsa.add r<0>, d(0x10000000000000000), r<2>

// -----

// expected-error@+1 {{69-bit immediate does not fit in 64-bit literal}}
dxsa.add r<0>, d(0x1FFFFFFFFFFFFFFFFF), r<2>

// -----

// expected-error@+1 {{immediate literal cannot carry a type}}
dxsa.add r<0>, l(42 : i13), r<2>

// -----

// expected-error@+1 {{expected integer literal}}
dxsa.add r<0>, l(true), r<2>

// -----

// expected-error@+1 {{immediate operands cannot have a source modifier}}
dxsa.add r<0>, -l(1), r<2>

// -----

// expected-error@+1 {{immediate operands cannot have a source modifier}}
dxsa.add r<0>, |d(1, 2)|, r<2>

// -----

// expected-error@+1 {{type `l` immediate has an invalid element count: 2}}
dxsa.add r<0>, l(0x1, 0x2), r<2>

// -----

// More than 4 elements overflows the widest `l` literal.
// expected-error@+1 {{type `l` immediate has an invalid element count: 5}}
dxsa.add r<0>, l(0x1, 0x2, 0x3, 0x4, 0x5), r<2>

// -----

// expected-error@+1 {{type `d` immediate has an invalid element count: 1}}
dxsa.add r<0>, d(0x1), r<2>

// -----

// expected-error@+1 {{type `d` immediate has an invalid element count: 3}}
dxsa.add r<0>, d(0x1, 0x2, 0x3), r<2>
