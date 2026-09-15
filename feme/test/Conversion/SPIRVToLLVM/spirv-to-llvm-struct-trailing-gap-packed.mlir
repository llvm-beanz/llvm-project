// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H135: a Block/Uniform struct member whose own natural (tightly
// packed) size undershoots the byte gap to its next sibling's declared
// offset -- here `vector<2xi32>` at offset 32 is only 8 bytes wide, but the
// next member (`vector<4xi32>`) is declared at offset 48, a gap that a
// non-packed `!llvm.struct` used to supply *implicitly*, via its own
// trailing per-member ABI-alignment padding. Since `layOutStructIfOffsetsMatch`
// always builds its result `packed` now (so that `!llvm.array<N x StructTy>`,
// which has no separate stride field, never silently gets the wrong
// per-element size), this gap must instead be a real, explicit
// `!llvm.array<8 x i8>` sibling member -- verify it is.

// CHECK: llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: -> !llvm.target<"spirv.VulkanBuffer", !llvm.struct<packed (vector<3xf32>, vector<4xi32>, vector<2xi32>, array<8 x i8>, vector<4xi32>)>, 2, 0>
// CHECK: %[[FIELD:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK-SAME: -> !llvm.ptr<12>
// CHECK: llvm.load %[[FIELD]] : !llvm.ptr<12> -> vector<2xi32>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @cb bind(0, 2) : !spirv.ptr<!spirv.struct<(!spirv.struct<CBVectors, (vector<3xf32> [0], vector<4xsi32> [16], vector<2xsi32> [32], vector<4xsi32> [48])> [0])>, Uniform>
  spirv.func @read() -> si32 "None" {
    %0 = spirv.mlir.addressof @cb : !spirv.ptr<!spirv.struct<(!spirv.struct<CBVectors, (vector<3xf32> [0], vector<4xsi32> [16], vector<2xsi32> [32], vector<4xsi32> [48])> [0])>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c2 = spirv.Constant 2 : si32
    %ac = spirv.AccessChain %0[%c0, %c2] : !spirv.ptr<!spirv.struct<(!spirv.struct<CBVectors, (vector<3xf32> [0], vector<4xsi32> [16], vector<2xsi32> [32], vector<4xsi32> [48])> [0])>, Uniform>, si32, si32 -> !spirv.ptr<vector<2xsi32>, Uniform>
    %v = spirv.Load "Uniform" %ac : vector<2xsi32>
    %e = spirv.CompositeExtract %v[0 : i32] : vector<2xsi32>
    spirv.ReturnValue %e : si32
  }
}

// -----

// Roadmap H135: the same natural-alignment-gap materialization must also
// work correctly when this outer struct is itself embedded as a member of
// a *further* outer struct (rather than being the module-level Uniform/
// Block struct directly) -- this is the shape that exposed a second,
// subtler bug this same session: `mlir::DataLayout::getTypeABIAlignment`
// reports alignment 1 for any packed `!llvm.struct` (LLVM's own rule for a
// packed struct), which -- if used directly for this struct's own
// "is this gap natural" check -- makes the *inner* struct's real natural
// alignment (16, from its own `vector<4xi32>`/`vector<3xf32>` members)
// disappear once it is built packed, turning the gap between this outer
// struct's leading `f32` member and its nested-struct member (declared 16
// bytes later) into a false "interior" gap requiring `AllowInteriorPad`
// (never set on this function's own first, ordinary attempt) -- which used
// to make the whole outer struct's conversion fall through to unrelated,
// and here incorrect, later retry tiers instead of simply materializing an
// explicit leading pad, exactly as this test's own inner struct's trailing
// gap above is materialized. `getNaturalAlignmentIgnoringPacking` fixes
// this by computing a struct's own natural alignment recursively from its
// members, ignoring whatever `isPacked` its own already-built LLVM type
// carries.
//
// CHECK: llvm.mlir.global external @outer_block()
// CHECK-SAME: !llvm.struct<packed (f32, array<12 x i8>, struct<packed (vector<3xf32>, vector<4xi32>, vector<2xi32>, array<8 x i8>, vector<4xi32>)>, vector<4xf32>)>
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @outer_block : !spirv.ptr<!spirv.struct<(f32 [0], !spirv.struct<CBVectors2, (vector<3xf32> [0], vector<4xsi32> [16], vector<2xsi32> [32], vector<4xsi32> [48])> [16], vector<4xf32> [80]), BufferBlock>, StorageBuffer>
}
