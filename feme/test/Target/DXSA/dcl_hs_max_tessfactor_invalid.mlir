// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.dcl_hs_max_tessfactor' op MaxTessFactor must be in [1.0, 64.0], got 5.000000e-01}}
dxsa.dcl_hs_max_tessfactor 0.5

// -----

// expected-error@+1 {{'dxsa.dcl_hs_max_tessfactor' op MaxTessFactor must be in [1.0, 64.0], got -1.000000e+00}}
dxsa.dcl_hs_max_tessfactor -1.000000e+00

// -----

// expected-error@+1 {{'dxsa.dcl_hs_max_tessfactor' op MaxTessFactor must be in [1.0, 64.0], got 6.500000e+01}}
dxsa.dcl_hs_max_tessfactor 6.500000e+01

// -----

// expected-error@+1 {{'dxsa.dcl_hs_max_tessfactor' op MaxTessFactor must be in [1.0, 64.0], got nan}}
dxsa.dcl_hs_max_tessfactor 0x7FC00000

// -----

// expected-error@+1 {{'dxsa.dcl_hs_max_tessfactor' op MaxTessFactor must be in [1.0, 64.0], got INF}}
dxsa.dcl_hs_max_tessfactor 0x7F800000

// -----

// expected-error@+1 {{'dxsa.dcl_hs_max_tessfactor' op MaxTessFactor must be in [1.0, 64.0], got -INF}}
dxsa.dcl_hs_max_tessfactor 0xFF800000
