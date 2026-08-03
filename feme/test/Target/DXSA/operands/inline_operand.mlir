// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// CHECK: dxsa.add r<0>, r<1>, r<2>
dxsa.add r<0>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0>, r<2>
dxsa.add r<0>, v<0>, r<2>

// -----

// CHECK: dxsa.add o<0>, r<1>, r<2>
dxsa.add o<0>, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0>, x<[1, 4]>, r<2>
dxsa.add r<0>, x<[1, 4]>, r<2>

// -----

// CHECK: dxsa.add r<0>, cb<[0, 5]>, r<2>
dxsa.add r<0>, cb<[0, 5]>, r<2>

// -----

// CHECK: dxsa.add r<0>, icb<5>, r<2>
dxsa.add r<0>, icb<5>, r<2>

// -----

// CHECK: dxsa.add r<0>, s<0>, r<2>
dxsa.add r<0>, s<0>, r<2>

// -----

// CHECK: dxsa.add r<0>, t<0>, r<2>
dxsa.add r<0>, t<0>, r<2>

// -----

// CHECK: dxsa.add r<0>, u<0>, r<2>
dxsa.add r<0>, u<0>, r<2>

// -----

// CHECK: dxsa.add r<0>, g<0>, r<2>
dxsa.add r<0>, g<0>, r<2>

// -----

// CHECK: dxsa.add r<0>, m<0>, r<2>
dxsa.add r<0>, m<0>, r<2>

// -----

// CHECK: dxsa.add null, r<1>, r<2>
dxsa.add null, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0>, vPrim, r<2>
dxsa.add r<0>, vPrim, r<2>

// -----

// CHECK: dxsa.add r<0>, vCoverage, r<2>
dxsa.add r<0>, vCoverage, r<2>

// -----

// CHECK: dxsa.add oDepth, r<1>, r<2>
dxsa.add oDepth, r<1>, r<2>

// -----

// CHECK: dxsa.add r<0>, vDomain, r<2>
dxsa.add r<0>, vDomain, r<2>

// -----

// CHECK: dxsa.add r<0>, -|r<1, min16f, nonuniform, <y, x, w, z>>|, r<2>
dxsa.add r<0>, -|r<1, min16f, nonuniform, <y, x, w, z>>|, r<2>

// -----

// CHECK: dxsa.add r<0>, r<1, min16f, nonuniform, <x>>, r<2>
dxsa.add r<0>, r<1, nonuniform, <x>, min16f>, r<2>
