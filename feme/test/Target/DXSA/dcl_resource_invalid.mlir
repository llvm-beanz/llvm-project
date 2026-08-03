// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.dcl_resource' op missing sample_count for multisampled dimension texture2dms}}
dxsa.dcl_resource <id = 0>, <dim = texture2dms>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op missing sample_count for multisampled dimension texture2dmsarray}}
dxsa.dcl_resource <id = 0>, <dim = texture2dmsarray>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op sample_count is only valid for texture2dms and texture2dmsarray, got buffer}}
dxsa.dcl_resource <id = 0>, <dim = buffer, sample_count = 4>, <x = unorm, y = snorm, z = sint, w = uint>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op sample_count is only valid for texture2dms and texture2dmsarray, got texture1d}}
dxsa.dcl_resource <id = 0>, <dim = texture1d, sample_count = 4>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op sample_count is only valid for texture2dms and texture2dmsarray, got texture1darray}}
dxsa.dcl_resource <id = 0>, <dim = texture1darray, sample_count = 4>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op sample_count is only valid for texture2dms and texture2dmsarray, got texture2d}}
dxsa.dcl_resource <id = 0>, <dim = texture2d, sample_count = 4>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op sample_count is only valid for texture2dms and texture2dmsarray, got texture2darray}}
dxsa.dcl_resource <id = 0>, <dim = texture2darray, sample_count = 4>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op sample_count is only valid for texture2dms and texture2dmsarray, got texture3d}}
dxsa.dcl_resource <id = 0>, <dim = texture3d, sample_count = 4>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op sample_count is only valid for texture2dms and texture2dmsarray, got texturecube}}
dxsa.dcl_resource <id = 0>, <dim = texturecube, sample_count = 4>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op sample_count is only valid for texture2dms and texture2dmsarray, got texturecubearray}}
dxsa.dcl_resource <id = 0>, <dim = texturecubearray, sample_count = 4>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{attribute 'sample_count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 127}}
dxsa.dcl_resource <id = 0>, <dim = texture2dms, sample_count = 0>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{attribute 'sample_count' failed to satisfy constraint: 32-bit signless integer attribute whose value is positive whose maximum value is 127}}
dxsa.dcl_resource <id = 0>, <dim = texture2dms, sample_count = 128>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op expected lbound <= ubound, got lbound=5, ubound=3}}
dxsa.dcl_resource <id = 0, lbound = 5, ubound = 3, space = 1>, <dim = buffer>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op expected lbound <= ubound, got lbound=5, ubound=3}}
dxsa.dcl_resource <id = 0, lbound = 5, ubound = 3, space = 1>, <dim = buffer>, <x = float, y = float, z = float, w = float>

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_resource"() {id = 0 : i32, dim = #dxsa<resource_dimension buffer>,
  x = #dxsa<resource_return_type float>, y = #dxsa<resource_return_type float>,
  z = #dxsa<resource_return_type float>, w = #dxsa<resource_return_type float>,
  lbound = 0 : i32} : () -> ()

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_resource"() {id = 0 : i32, dim = #dxsa<resource_dimension buffer>,
  x = #dxsa<resource_return_type float>, y = #dxsa<resource_return_type float>,
  z = #dxsa<resource_return_type float>, w = #dxsa<resource_return_type float>,
  ubound = 3 : i32} : () -> ()

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_resource"() {id = 0 : i32, dim = #dxsa<resource_dimension buffer>,
  x = #dxsa<resource_return_type float>, y = #dxsa<resource_return_type float>,
  z = #dxsa<resource_return_type float>, w = #dxsa<resource_return_type float>,
  space = 1 : i32} : () -> ()

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_resource"() {id = 0 : i32, dim = #dxsa<resource_dimension buffer>,
  x = #dxsa<resource_return_type float>, y = #dxsa<resource_return_type float>,
  z = #dxsa<resource_return_type float>, w = #dxsa<resource_return_type float>,
  lbound = 0 : i32, ubound = 3 : i32} : () -> ()

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_resource"() {id = 0 : i32, dim = #dxsa<resource_dimension buffer>,
  x = #dxsa<resource_return_type float>, y = #dxsa<resource_return_type float>,
  z = #dxsa<resource_return_type float>, w = #dxsa<resource_return_type float>,
  lbound = 0 : i32, space = 1 : i32} : () -> ()

// -----

// expected-error@+1 {{'dxsa.dcl_resource' op lbound, ubound and space must be either all set or all absent}}
"dxsa.dcl_resource"() {id = 0 : i32, dim = #dxsa<resource_dimension buffer>,
  x = #dxsa<resource_return_type float>, y = #dxsa<resource_return_type float>,
  z = #dxsa<resource_return_type float>, w = #dxsa<resource_return_type float>,
  ubound = 3 : i32, space = 1 : i32} : () -> ()
