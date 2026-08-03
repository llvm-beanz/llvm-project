// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{attribute 'count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 32}}
dxsa.dcl_gs_instance_count -1

// -----

// expected-error@+1 {{attribute 'count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 32}}
dxsa.dcl_gs_instance_count 0

// -----

// expected-error@+1 {{attribute 'count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 32}}
dxsa.dcl_gs_instance_count 33
