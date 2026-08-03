// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// A four-element 32-bit literal (1.0, 2.0, 3.0, 4.0).
// CHECK: dxsa.add r<0>, l(0x3F800000, 0x40000000, 0x40400000, 0x40800000), r<2>
dxsa.add r<0>, l(0x3F800000, 0x40000000, 0x40400000, 0x40800000), r<2>

// -----

// A two-element 64-bit literal (1.0, 2.0).
// CHECK: dxsa.add r<0>, d(0x3FF0000000000000, 0x4000000000000000), r<2>
dxsa.add r<0>, d(0x3FF0000000000000, 0x4000000000000000), r<2>

// -----

// A single-element scalar literal. Leading zeros are trimmed on print.
// CHECK: dxsa.add r<0>, l(0x2A), r<2>
dxsa.add r<0>, l(0x0000002A), r<2>

// -----

// Leading zeros in the input are trimmed on print.
// CHECK: dxsa.add r<0>, l(0x2, 0xF, 0x100, 0x1000), r<2>
dxsa.add r<0>, l(0x00000002, 0x0000000F, 0x00000100, 0x00001000), r<2>

// -----

// CHECK: dxsa.add r<0>, l(0x0), r<2>
dxsa.add r<0>, l(0x0), r<2>

// -----

// Decimal integers are accepted on input and normalized to hex.
// CHECK: dxsa.add r<0>, l(0x1, 0x2, 0x3, 0x4), r<2>
dxsa.add r<0>, l(1, 2, 3, 4), r<2>

// -----

// CHECK: dxsa.add r<0>, l(0x80000000, 0x7FFFFFFF, 0xFFFFFFFF, 0x0), r<2>
dxsa.add r<0>, l(0x80000000, 0x7FFFFFFF, -1, 0), r<2>

// -----

// CHECK: dxsa.add r<0>, d(0x2, 0xFFFFFFFFFFFFFFFF), r<2>
dxsa.add r<0>, d(0x0000000000000002, -1), r<2>
