// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{attribute 'count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 4096}}
dxsa.dcl_temps 0

// -----

// expected-error@+1 {{attribute 'count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 4096}}
dxsa.dcl_temps 4097
