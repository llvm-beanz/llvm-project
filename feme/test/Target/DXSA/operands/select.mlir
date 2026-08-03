// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// CHECK: dxsa.add r<0>, v<0, <x>>, r<2>
dxsa.add r<0>, v<0, <x>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, <y>>, r<2>
dxsa.add r<0>, v<0, <y>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, <z>>, r<2>
dxsa.add r<0>, v<0, <z>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, <w>>, r<2>
dxsa.add r<0>, v<0, <w>>, r<2>
