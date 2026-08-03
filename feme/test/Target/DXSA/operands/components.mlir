// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// Canonical: `r` is implicitly `vector`, the keyword is omitted.
// CHECK: dxsa.add r<0>, r<1>, r<2>
dxsa.add r<0>, r<1>, r<2>

// -----

// Override: explicitly downsize an `r` to `scalar`.
// CHECK: dxsa.add r<0>, r<1, scalar>, r<2>
dxsa.add r<0>, r<1, scalar>, r<2>

// -----

// Override: explicitly request `none` on an `r` (unusual but legal).
// CHECK: dxsa.add r<0>, r<1, none>, r<2>
dxsa.add r<0>, r<1, none>, r<2>

// -----

// Override: explicit `reserved` (DXBC `N_COMPONENT`) preserved for round-trip.
// CHECK: dxsa.add r<0>, r<1, reserved>, r<2>
dxsa.add r<0>, r<1, reserved>, r<2>

// -----

// Canonical: `g` is implicitly `none` (a handle), the keyword is omitted.
// CHECK: dxsa.add r<0>, g<0>, r<2>
dxsa.add r<0>, g<0>, r<2>

// -----

// CHECK: dxsa.add r<0>, g<0, vector>, r<2>
dxsa.add r<0>, g<0, vector>, r<2>

// -----

// Canonical: `vPrim` is implicitly `scalar`. With no other body field set
// the operand prints as the bare type keyword without `<...>`.
// CHECK: dxsa.add r<0>, vPrim, r<2>
dxsa.add r<0>, vPrim, r<2>

// -----

// Override: explicitly request `vector` on `vPrim`.
// CHECK: dxsa.add r<0>, vPrim<vector>, r<2>
dxsa.add r<0>, vPrim<vector>, r<2>
