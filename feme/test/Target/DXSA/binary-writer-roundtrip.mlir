// RUN: feme-translate --export-dxsa-bin %s -o %t.bin
// RUN: feme-translate --import-dxsa-bin %t.bin | FileCheck %s

// Round-trips a `dxsa` module through `feme::dxsa::serialize`
// (`--export-dxsa-bin`) and back through `feme::dxsa::deserialize`
// (`--import-dxsa-bin`), exercising every generic operand shape
// BinaryWriter.cpp supports: `DXSA_UnaryOp` (mov, itof), `DXSA_BinaryOp`
// (add_sat, a distinct dxsa op from `add` -- see BinaryWriter.cpp's file
// comment on how `_sat` is stripped back to the base opcode),
// `DXSA_MultiplyAddOp` (mad), `DXSA_MovConditionalOp` (movc), an
// index/swizzle/writemask-bearing register operand, and a 32-bit immediate
// operand. Declarations (`dcl_*`) are deliberately not used here: they are
// not yet one of BinaryWriter's supported shapes (see its file comment),
// so a real shader still cannot round-trip through it end to end.

dxsa.module compute_shader 5 0 {
  // CHECK: dxsa.mad r<0>, r<0>, r<0>, r<0>
  dxsa.mad r<0>, r<0>, r<0>, r<0>
  // CHECK: dxsa.add_sat r<1, <x>>, r<0, <x>>, l(0x3F800000)
  dxsa.add_sat r<1, <x>>, r<0, <x>>, l(0x3F800000)
  // CHECK: dxsa.mov r<0, <x, y>>, r<1, <x, x, y, y>>
  dxsa.mov r<0, <x, y>>, r<1, <x, x, y, y>>
  // CHECK: dxsa.itof r<1, <y>>, r<0, <x>>
  dxsa.itof r<1, <y>>, r<0, <x>>
  // CHECK: dxsa.movc r<0>, r<1, <x, x, x, x>>, r<0>, r<1>
  dxsa.movc r<0>, r<1, <x, x, x, x>>, r<0>, r<1>
  // CHECK: dxsa.ret
  dxsa.ret
}
