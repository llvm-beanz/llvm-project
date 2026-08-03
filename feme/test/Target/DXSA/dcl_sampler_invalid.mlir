// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{expected lbound <= ubound, got lbound=5, ubound=3}}
dxsa.dcl_sampler <id = 0, mode = default, lbound = 5, ubound = 3, space = 1>
