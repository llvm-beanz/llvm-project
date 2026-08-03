// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// Negation.
// CHECK: dxsa.add r<0>, -r<1>, r<2>
dxsa.add r<0>, -r<1>, r<2>

// -----

// Absolute value.
// CHECK: dxsa.add r<0>, |r<1>|, r<2>
dxsa.add r<0>, |r<1>|, r<2>

// -----

// Negated absolute value.
// CHECK: dxsa.add r<0>, -|r<1>|, r<2>
dxsa.add r<0>, -|r<1>|, r<2>
