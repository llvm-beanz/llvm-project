// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// Identity swizzle on a source collapses to the canonical print.
// CHECK: dxsa.add r<0>, v<0>, r<2>
dxsa.add r<0>, v<0, <x, y, z, w>>, r<2>

// -----

// Order is preserved in the print.
// CHECK: dxsa.add r<0>, v<0, <w, z, y, x>>, r<2>
dxsa.add r<0>, v<0, <w, z, y, x>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, <x, x, x, x>>, r<2>
dxsa.add r<0>, v<0, <x, x, x, x>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, <y, y, y, y>>, r<2>
dxsa.add r<0>, v<0, <y, y, y, y>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, <z, z, z, z>>, r<2>
dxsa.add r<0>, v<0, <z, z, z, z>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, <w, w, w, w>>, r<2>
dxsa.add r<0>, v<0, <w, w, w, w>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, <x, y, x, y>>, r<2>
dxsa.add r<0>, v<0, <x, y, x, y>>, r<2>
