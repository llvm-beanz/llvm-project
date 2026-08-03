// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.dcl_uav_raw' op hasOrderPreservingCounter flag is only valid for dcl_uav_structured}}
dxsa.dcl_uav_raw <id = 0>, <flags = hasOrderPreservingCounter>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_raw' op expected lbound <= ubound, got lbound=5, ubound=3}}
dxsa.dcl_uav_raw <id = 0, lbound = 5, ubound = 3, space = 1>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_raw' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_uav_raw"() {id = 0 : i32, lbound = 0 : i32} : () -> ()
