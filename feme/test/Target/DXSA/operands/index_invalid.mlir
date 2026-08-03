// RUN: feme-opt %s -split-input-file -verify-diagnostics

// An index immediate must be 32- or 64-bit; other widths are rejected.
// expected-error@+1 {{unsupported index type: 'i17'}}
dxsa.add r<0>, v<42 : i17>, r<2>

// -----

// expected-error@+1 {{unsupported index type: 'i43'}}
dxsa.add r<0>, cb<[1, 2 : i43]>, r<2>

// -----

// A non-integer index type is rejected at parse time.
// expected-error@+1 {{invalid kind of type specified: expected builtin.integer}}
dxsa.add r<0>, v<42 : f32>, r<2>

// -----

// expected-error@+1 {{4294967296 does not fit in 'i32'}}
dxsa.add r<0>, v<4294967296 : i32>, r<2>

// -----

// A value that overflows i32 must be spelled with an explicit `: i64`.
// expected-error@+1 {{4294967296 does not fit in 'i32'}}
dxsa.add r<0>, v<4294967296>, r<2>
