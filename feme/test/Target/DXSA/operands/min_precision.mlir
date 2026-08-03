// RUN: feme-opt %s -split-input-file --verify-roundtrip | FileCheck %s

// CHECK: dxsa.add r<0>, v<0, min16f, <x>>, r<2>
dxsa.add r<0>, v<0, min16f, <x>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, min2_8f, <x>>, r<2>
dxsa.add r<0>, v<0, min2_8f, <x>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, min16i, <x>>, r<2>
dxsa.add r<0>, v<0, min16i, <x>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, min16u, <x>>, r<2>
dxsa.add r<0>, v<0, min16u, <x>>, r<2>

// -----

// CHECK: dxsa.add r<0>, v<0, min16f>, r<2>
dxsa.add r<0>, v<0, min16f>, r<2>
