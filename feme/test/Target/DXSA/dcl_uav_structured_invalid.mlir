// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.dcl_uav_structured' op struct byte stride must be a multiple of 4, got 6}}
dxsa.dcl_uav_structured <id = 0, struct_byte_stride = 6>

// -----

// expected-error@+1 {{attribute 'struct_byte_stride' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive}}
dxsa.dcl_uav_structured <id = 0, struct_byte_stride = 0>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_structured' op expected lbound <= ubound, got lbound=5, ubound=3}}
dxsa.dcl_uav_structured <id = 0, struct_byte_stride = 16, lbound = 5, ubound = 3, space = 1>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_structured' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_uav_structured"() {id = 0 : i32, struct_byte_stride = 16 : i32,
                             lbound = 0 : i32} : () -> ()
