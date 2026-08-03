// RUN: feme-opt %s -split-input-file -verify-diagnostics

// expected-error@+1 {{'dxsa.module' op attribute 'major_version' failed to satisfy constraint: 32-bit signless integer attribute whose minimum value is 0 whose maximum value is 255}}
"dxsa.module"() <{major_version = 256 : i32, minor_version = 0 : i32, program_type = #dxsa<program_type pixel_shader>}> ({
^bb0:
}) : () -> ()

// -----

// expected-error@+1 {{'dxsa.module' op attribute 'minor_version' failed to satisfy constraint: 32-bit signless integer attribute whose minimum value is 0 whose maximum value is 255}}
"dxsa.module"() <{major_version = 5 : i32, minor_version = 256 : i32, program_type = #dxsa<program_type pixel_shader>}> ({
^bb0:
}) : () -> ()

// -----

// expected-error@+1 {{'dxsa.module' op program_type, major_version and minor_version must all be present or all absent}}
"dxsa.module"() <{program_type = #dxsa<program_type pixel_shader>}> ({
^bb0:
}) : () -> ()

// -----

// expected-error@+1 {{'dxsa.module' op program_type, major_version and minor_version must all be present or all absent}}
"dxsa.module"() <{major_version = 5 : i32, program_type = #dxsa<program_type pixel_shader>}> ({
^bb0:
}) : () -> ()

// -----

// expected-error@+1 {{'dxsa.module' op program_type, major_version and minor_version must all be present or all absent}}
"dxsa.module"() <{minor_version = 1 : i32, program_type = #dxsa<program_type pixel_shader>}> ({
^bb0:
}) : () -> ()
