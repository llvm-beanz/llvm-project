// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// L263: `BitFieldInsertPattern`/`BitFieldSExtractPattern`/
// `BitFieldUExtractPattern` each had a bug reachable only at `Count`'s two
// closed-interval endpoints (`Count == 0` and `Count == Size`, both
// spec-legal: "insert/extract nothing" and "the whole field", respectively):
//
// - `BitFieldInsertPattern` ORed in `Insert << Offset` *unmasked* into the
//   result -- correct for interior `Count` values (where the surrounding
//   `Base`-clearing mask already zeroes out the bits that matter), but
//   `Insert`'s bits above `Count - 1` are unspecified by the SPIR-V spec and
//   routinely nonzero in practice, corrupting bits above the intended field
//   for every `Count` value, not just the two endpoints -- reduced here as
//   `insert_wide_insert_operand`, using a `Count` well inside the interior
//   to isolate this from the two edge-case bugs below.
// - `BitFieldSExtractPattern` computed an `llvm.ashr` shift amount that
//   reduces to exactly `Size` whenever `Count == 0` (regardless of
//   `Offset`) -- poison per the LLVM LangRef, instead of the spec-mandated
//   result of `0`.
// - `BitFieldUExtractPattern` (and `BitFieldInsertPattern`'s own internal
//   field-mask construction) built their "low `Count` bits set" mask via
//   `(-1 << Count) ^ -1`, which requires an out-of-range (`Count == Size`)
//   `llvm.shl` -- also poison -- instead of the spec-mandated all-ones mask.
//
// All three are fixed by `createBitFieldLowMask` (clamped-shift-then-select
// construction, used by both `BitFieldInsertPattern`'s field mask and
// `BitFieldUExtractPattern`'s own mask) and a `Count == 0`-guarded
// `llvm.select` around `BitFieldSExtractPattern`'s final `llvm.ashr`.
// Reduced from the real CTS failures found by a fresh full
// `dEQP-VK.glsl.*` sweep: `dEQP-VK.glsl.builtin.function.integer.
// {bitfieldinsert,bitfieldextract}.*` (`bits=0` and `offset=0,bits=32`
// inputs specifically).

// CHECK-LABEL: llvm.func @uextract_count_zero_or_full
// CHECK: llvm.intr.umin
// CHECK: llvm.icmp "uge"
// CHECK: llvm.select
// CHECK: llvm.lshr
// CHECK: llvm.and
// CHECK: llvm.return
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.func @uextract_count_zero_or_full(%base : i32, %offset : i32, %count : i32) -> i32 "None" {
    %r = spirv.BitFieldUExtract %base, %offset, %count : i32, i32, i32
    spirv.ReturnValue %r : i32
  }

  // CHECK-LABEL: llvm.func @sextract_count_zero
  // CHECK: llvm.shl
  // CHECK: llvm.icmp "eq"
  // CHECK: llvm.select
  // CHECK: llvm.ashr
  // CHECK: llvm.select
  // CHECK: llvm.return
  spirv.func @sextract_count_zero(%base : i32, %offset : i32, %count : i32) -> i32 "None" {
    %r = spirv.BitFieldSExtract %base, %offset, %count : i32, i32, i32
    spirv.ReturnValue %r : i32
  }

  // CHECK-LABEL: llvm.func @insert_wide_insert_operand
  // CHECK: llvm.intr.umin
  // CHECK: llvm.icmp "uge"
  // CHECK: llvm.select
  // CHECK: llvm.shl
  // CHECK: llvm.xor
  // CHECK: llvm.and
  // CHECK: llvm.shl
  // -- the fix: `Insert << Offset` must be masked to the field width before
  // being OR'd into the result, so an `llvm.and` must appear between the
  // second `llvm.shl` (computing `Insert << Offset`) and the final `llvm.or`.
  // CHECK: llvm.and
  // CHECK: llvm.or
  // CHECK: llvm.return
  spirv.func @insert_wide_insert_operand(%base : i32, %insert : i32, %offset : i32, %count : i32) -> i32 "None" {
    %r = spirv.BitFieldInsert %base, %insert, %offset, %count : i32, i32, i32
    spirv.ReturnValue %r : i32
  }
}
