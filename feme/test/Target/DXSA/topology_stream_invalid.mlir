// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{attribute 'index' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 3}}
dxsa.emit_stream -1

// -----

// expected-error@+1 {{attribute 'index' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 3}}
dxsa.emit_stream 4

// -----

// expected-error@+1 {{attribute 'index' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 3}}
dxsa.cut_stream -1

// -----

// expected-error@+1 {{attribute 'index' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 3}}
dxsa.cut_stream 4

// -----

// expected-error@+1 {{attribute 'index' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 3}}
dxsa.emit_then_cut_stream -1

// -----

// expected-error@+1 {{attribute 'index' failed to satisfy constraint: 32-bit signless integer attribute whose value is non-negative whose maximum value is 3}}
dxsa.emit_then_cut_stream 4
