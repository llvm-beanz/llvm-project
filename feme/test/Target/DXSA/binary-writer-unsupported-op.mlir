// RUN: not feme-translate --export-dxsa-bin %s 2>&1 | FileCheck %s

// `feme::dxsa::serialize` (BinaryWriter.cpp) must diagnose an operation it
// does not yet know how to encode -- a declaration, control-flow op, or
// resource/texture op, none of which are one of the generic operand shapes
// it currently supports (see the file comment) -- rather than silently
// dropping or mis-encoding it.

dxsa.module compute_shader 5 0 {
  dxsa.dcl_temps 1
  dxsa.ret
}

// CHECK: error: cannot serialize this 'dxsa' operation to DXBC binary
