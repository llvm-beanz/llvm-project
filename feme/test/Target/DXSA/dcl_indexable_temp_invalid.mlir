// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{attribute 'size' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 4096}}
dxsa.dcl_indexable_temp x<0>[0], 1

// -----

// expected-error@+1 {{attribute 'size' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 4096}}
dxsa.dcl_indexable_temp x<0>[4097], 1

// -----

// expected-error@+1 {{attribute 'num_components' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 4}}
dxsa.dcl_indexable_temp x<0>[16], 0

// -----

// expected-error@+1 {{attribute 'num_components' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 4}}
dxsa.dcl_indexable_temp x<0>[16], 5

// -----

// expected-error@+1 {{expected 'x'}}
dxsa.dcl_indexable_temp r<0>[16], 1
