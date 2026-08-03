// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{attribute 'count' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 32}}
dxsa.dcl_output_control_point_count -1

// -----

// expected-error@+1 {{attribute 'count' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 32}}
dxsa.dcl_output_control_point_count 33
