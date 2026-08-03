// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// CHECK: dxsa.add r<0>, r<1, nonuniform>, r<2>
dxsa.add r<0>, r<1, nonuniform>, r<2>

// -----

// Combined with min precision: ordering in the print is fixed.
// CHECK: dxsa.add r<0>, r<1, min16f, nonuniform>, r<2>
dxsa.add r<0>, r<1, nonuniform, min16f>, r<2>

// -----

// CHECK: dxsa.add r<0>, t<5, nonuniform>, r<2>
dxsa.add r<0>, t<5, nonuniform>, r<2>
