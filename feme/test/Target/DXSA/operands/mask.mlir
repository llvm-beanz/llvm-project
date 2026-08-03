// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// CHECK: dxsa.add r<0, <x>>, r<1>, r<2>
dxsa.add r<0, <x>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0, <y>>, r<1>, r<2>
dxsa.add r<0, <y>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0, <z>>, r<1>, r<2>
dxsa.add r<0, <z>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0, <w>>, r<1>, r<2>
dxsa.add r<0, <w>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0, <x, y>>, r<1>, r<2>
dxsa.add r<0, <x, y>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0, <x, w>>, r<1>, r<2>
dxsa.add r<0, <x, w>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0, <z, w>>, r<1>, r<2>
dxsa.add r<0, <z, w>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0, <x, y, z>>, r<1>, r<2>
dxsa.add r<0, <x, y, z>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0, <x, z, w>>, r<1>, r<2>
dxsa.add r<0, <x, z, w>>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0>, r<1>, r<2>
dxsa.add r<0, <x, y, z, w>>, r<1>, r<2>

// -----

// Mask components are normalized to canonical order in the print, regardless
// of the input order.
// CHECK: dxsa.add r<0, <x, y, z>>, r<1>, r<2>
dxsa.add r<0, <z, y, x>>, r<1>, r<2>
