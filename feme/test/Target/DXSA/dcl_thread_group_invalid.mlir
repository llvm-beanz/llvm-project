// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{attribute 'x' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 1024}}
dxsa.dcl_thread_group <x = 0, y = 1, z = 1>

// -----

// expected-error@+1 {{attribute 'x' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 1024}}
dxsa.dcl_thread_group <x = 1025, y = 1, z = 1>

// -----

// expected-error@+1 {{attribute 'y' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 1024}}
dxsa.dcl_thread_group <x = 1, y = 0, z = 1>

// -----

// expected-error@+1 {{attribute 'y' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 1024}}
dxsa.dcl_thread_group <x = 1, y = 1025, z = 1>

// -----

// expected-error@+1 {{attribute 'z' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 64}}
dxsa.dcl_thread_group <x = 1, y = 1, z = 0>

// -----

// expected-error@+1 {{attribute 'z' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 64}}
dxsa.dcl_thread_group <x = 1, y = 1, z = 65>

// -----

// 64 * 8 * 4 == 2048
// expected-error@+1 {{'dxsa.dcl_thread_group' op thread group size x*y*z must be <= 1024, got 2048}}
dxsa.dcl_thread_group <x = 64, y = 8, z = 4>
