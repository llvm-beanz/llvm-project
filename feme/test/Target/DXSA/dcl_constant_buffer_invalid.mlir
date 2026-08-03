// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{expected lbound <= ubound, got lbound=5, ubound=3}}
dxsa.dcl_constant_buffer <id = 0, size = 4, lbound = 5, ubound = 3, space = 1>, <dynamicIndexed>
