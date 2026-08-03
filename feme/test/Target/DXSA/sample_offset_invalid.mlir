// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{sample offsets must be 4 bit 2's complement numbers, having integer range [-8,7]}}
dxsa.sample r<1>, v<0, <x, y, x, x>>, t<3, vector>, s<5>, <u = -9, v = 7, w = 0>

// -----

// expected-error@+1 {{sample offsets must be 4 bit 2's complement numbers, having integer range [-8,7]}}
dxsa.sample r<1>, v<0, <x, y, x, x>>, t<3, vector>, s<5>, <u = 8, v = 7, w = 0>

// -----

// expected-error@+1 {{sample offsets must be 4 bit 2's complement numbers, having integer range [-8,7]}}
dxsa.sample r<1>, v<0, <x, y, x, x>>, t<3, vector>, s<5>, <u = -5, v = -9, w = 0>

// -----

// expected-error@+1 {{sample offsets must be 4 bit 2's complement numbers, having integer range [-8,7]}}
dxsa.sample r<1>, v<0, <x, y, x, x>>, t<3, vector>, s<5>, <u = -5, v = 8, w = 0>

// -----

// expected-error@+1 {{sample offsets must be 4 bit 2's complement numbers, having integer range [-8,7]}}
dxsa.sample r<1>, v<0, <x, y, x, x>>, t<3, vector>, s<5>, <u = -5, v = -4, w = -9>

// -----

// expected-error@+1 {{sample offsets must be 4 bit 2's complement numbers, having integer range [-8,7]}}
dxsa.sample r<1>, v<0, <x, y, x, x>>, t<3, vector>, s<5>, <u = -5, v = -4, w = 8>
