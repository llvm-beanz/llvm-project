// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{attribute 'index' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 3}}
dxsa.dcl_stream -1

// -----

// expected-error@+1 {{attribute 'index' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 3}}
dxsa.dcl_stream 4
