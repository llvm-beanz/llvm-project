// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.dcl_uav_typed' op invalid dimension for typed UAV: texture2dms}}
dxsa.dcl_uav_typed <id = 0, dim = texture2dms>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_typed' op invalid dimension for typed UAV: texture2dmsarray}}
dxsa.dcl_uav_typed <id = 0, dim = texture2dmsarray>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_typed' op invalid dimension for typed UAV: texturecube}}
dxsa.dcl_uav_typed <id = 0, dim = texturecube>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_typed' op invalid dimension for typed UAV: texturecubearray}}
dxsa.dcl_uav_typed <id = 0, dim = texturecubearray>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_typed' op hasOrderPreservingCounter flag is only valid for dcl_uav_structured}}
dxsa.dcl_uav_typed <id = 0, dim = buffer>, <x = float, y = float, z = float, w = float>, <flags = hasOrderPreservingCounter>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_typed' op expected lbound <= ubound, got lbound=5, ubound=3}}
dxsa.dcl_uav_typed <id = 0, dim = buffer, lbound = 5, ubound = 3, space = 1>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_uav_typed' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_uav_typed"() {id = 0 : i32, dim = #dxsa<resource_dimension buffer>,
                       x = #dxsa<resource_return_type float>,
                       y = #dxsa<resource_return_type float>,
                       z = #dxsa<resource_return_type float>,
                       w = #dxsa<resource_return_type float>,
                       lbound = 0 : i32} : () -> ()
