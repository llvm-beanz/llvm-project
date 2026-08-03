// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.dcl_index_range' op operand must be an input, output or patch-constant register, got r}}
dxsa.dcl_index_range r<0, <x, y, z, w>>, 3

// -----

// expected-error@+1 {{'dxsa.dcl_index_range' op operand must be an input, output or patch-constant register, got vicp}}
dxsa.dcl_index_range vicp<0, <x, y, z, w>>, 3

// -----

// expected-error@+1 {{attribute 'count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive}}
dxsa.dcl_index_range v<0, <x, y, z, w>>, 0
