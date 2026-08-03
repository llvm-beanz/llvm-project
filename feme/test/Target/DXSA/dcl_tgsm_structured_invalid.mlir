// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.dcl_tgsm_structured' op struct byte stride must be a multiple of 4, got 6}}
dxsa.dcl_tgsm_structured g<0>, 6, 64

// -----

// expected-error@+1 {{attribute 'struct_byte_stride' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 32768}}
dxsa.dcl_tgsm_structured g<0>, 0, 64

// -----

// expected-error@+1 {{attribute 'struct_byte_stride' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 32768}}
dxsa.dcl_tgsm_structured g<0>, 32772, 1

// -----

// expected-error@+1 {{attribute 'struct_count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 8192}}
dxsa.dcl_tgsm_structured g<0>, 16, 0

// -----

// expected-error@+1 {{attribute 'struct_count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 8192}}
dxsa.dcl_tgsm_structured g<0>, 4, 8193

// -----

// expected-error@+1 {{'dxsa.dcl_tgsm_structured' op total size struct_byte_stride * struct_count must be <= 32768, got 65536}}
dxsa.dcl_tgsm_structured g<0>, 32768, 2
