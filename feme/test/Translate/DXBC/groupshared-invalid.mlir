// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// RUN: split-file %s %t
// RUN: not feme-translate --dxsa-to-llvmir %t/duplicate.mlir 2>&1 | FileCheck %s --check-prefix=DUPLICATE
// RUN: not feme-translate --dxsa-to-llvmir %t/overflow.mlir 2>&1 | FileCheck %s --check-prefix=OVERFLOW
// RUN: not feme-translate --dxsa-to-llvmir %t/undeclared.mlir 2>&1 | FileCheck %s --check-prefix=UNDECLARED
// RUN: not feme-translate --dxsa-to-llvmir %t/mismatch.mlir 2>&1 | FileCheck %s --check-prefix=MISMATCH
// RUN: not feme-translate --dxsa-to-llvmir %t/register.mlir 2>&1 | FileCheck %s --check-prefix=REGISTER
// RUN: not feme-translate --dxsa-to-llvmir %t/stage.mlir 2>&1 | FileCheck %s --check-prefix=REGISTER
// RUN: not feme-translate --dxsa-to-llvmir %t/alignment.mlir 2>&1 | FileCheck %s --check-prefix=ALIGNMENT
// RUN: not feme-translate --dxsa-to-llvmir %t/feedback.mlir 2>&1 | FileCheck %s --check-prefix=FEEDBACK
// RUN: not feme-translate --dxsa-to-llvmir %t/mask.mlir 2>&1 | FileCheck %s --check-prefix=MASK
// RUN: not feme-translate --dxsa-to-llvmir %t/typed-store.mlir 2>&1 | FileCheck %s --check-prefix=TYPED

// DUPLICATE: error: duplicate thread-group shared memory declaration
// OVERFLOW: error: combined thread-group shared memory exceeds 32768 bytes
// UNDECLARED: access to an undeclared or indexed groupshared register
// MISMATCH: raw/structured groupshared access mismatch
// REGISTER: expected a static groupshared register in compute
// ALIGNMENT: groupshared byte offset must be aligned to 4
// FEEDBACK: groupshared mapping feedback
// MASK: groupshared store mask must be x, xy, xyz or xyzw
// TYPED: groupshared memory requires a raw or structured store

//--- duplicate.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_tgsm_raw g<0>, 16
  dxsa.dcl_tgsm_structured g<0>, 4, 4
  dxsa.ret
}
//--- overflow.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_tgsm_raw g<0>, 32768
  dxsa.dcl_tgsm_structured g<1>, 4, 1
  dxsa.ret
}
//--- undeclared.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_temps 1
  dxsa.ld_raw r<0>, l(0), g<0, vector, <x, x, x, x>>
  dxsa.ret
}
//--- mismatch.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_temps 1
  dxsa.dcl_tgsm_raw g<0>, 16
  dxsa.ld_structured r<0>, l(0), l(0), g<0, vector, <x, x, x, x>>
  dxsa.ret
}
//--- register.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_tgsm_raw u<0>, 16
  dxsa.ret
}
//--- stage.mlir
dxsa.module pixel_shader 5 0 {
  dxsa.dcl_tgsm_raw g<0>, 16
  dxsa.ret
}
//--- alignment.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_tgsm_raw g<0>, 16
  dxsa.store_raw g<0, vector, <x>>, l(2), l(1)
  dxsa.ret
}
//--- feedback.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_temps 2
  dxsa.dcl_tgsm_raw g<0>, 16
  dxsa.ld_raw_s r<0>, l(0), g<0, vector, <x, x, x, x>>, r<1, <x>>
  dxsa.ret
}
//--- mask.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_tgsm_raw g<0>, 16
  dxsa.store_raw g<0, vector, <x, z>>, l(0), l(1)
  dxsa.ret
}
//--- typed-store.mlir
dxsa.module compute_shader 5 0 {
  dxsa.dcl_tgsm_raw g<0>, 16
  dxsa.store_uav_typed g<0, vector, <x>>, l(0), l(1)
  dxsa.ret
}
