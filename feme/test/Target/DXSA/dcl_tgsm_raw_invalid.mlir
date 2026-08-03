// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.dcl_tgsm_raw' op byte count must be a multiple of 4, got 42}}
dxsa.dcl_tgsm_raw g<0>, 42

// -----

// expected-error@+1 {{attribute 'byte_count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 32768}}
dxsa.dcl_tgsm_raw g<0>, 0

// -----

// expected-error@+1 {{attribute 'byte_count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 32768}}
dxsa.dcl_tgsm_raw g<0>, 32769
