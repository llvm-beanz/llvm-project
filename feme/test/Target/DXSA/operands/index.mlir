// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// CHECK: dxsa.add r<0>, v<0>, r<2>
dxsa.add r<0>, v<0>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<42>, r<2>
dxsa.add r<0>, v<42>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<42 : i64>, r<2>
dxsa.add r<0>, v<42 : i64>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<4294967296 : i64>, r<2>
dxsa.add r<0>, v<4294967296 : i64>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<r<3, <x>>>, r<2>
dxsa.add r<0>, v<r<3, <x>>>, r<2>

// -----

// CHECK: dxsa.add r<0>, cb<[0, 2 + r<1, <x>>]>, r<2>
dxsa.add r<0>, cb<[0, 2 + r<1, <x>>]>, r<2>

// -----

// CHECK: dxsa.add r<0>, cb<2 : i64 + r<3, <x>>>, r<2>
dxsa.add r<0>, cb<2 : i64 + r<3, <x>>>, r<2>

// -----

// CHECK: dxsa.add r<0>, cb<[1 : i64, 2 + r<3, <x>>, r<5, <y>>]>, r<2>
dxsa.add r<0>, cb<[1 : i64, 2 + r<3, <x>>, r<5, <y>>]>, r<2>

// -----

// CHECK: dxsa.add r<0>, vPrim, r<2>
dxsa.add r<0>, vPrim, r<2>
