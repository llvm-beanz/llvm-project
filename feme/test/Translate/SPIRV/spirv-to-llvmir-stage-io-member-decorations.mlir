// RUN: feme-translate --no-implicit-module --spirv-to-llvmir %s | FileCheck %s

// (Roadmap H2c) A builtin interface block's own per-member `BuiltIn`
// decorations (glslang's implicit `gl_PerVertex`, `{gl_Position,
// gl_PointSize}` here standing in for the full four-member block) survive
// the whole `spirv` dialect -> LLVM IR translation as
// `!feme.spirv.MemberDecorations` metadata: `(memberIndex, decorations)`
// pairs, one per member, each `decorations` in the same `(i32 decoration,
// i32 arg...)` tuple shape `!spirv.Decorations` itself uses (see
// spirv-to-llvmir-stage-io.mlir). There is no real SPIR-V-backend metadata
// this becomes -- `OpMemberDecorate` decorates the block's *type*, not this
// global -- it exists purely for feme::graphics::CanonicalizeStagePass
// (roadmap H2d) to read later, recovering which system value each member
// corresponds to.
//
// (Roadmap L103) This struct's own LLVM type is now always built
// explicitly packed, with any padding a member's own natural ABI
// alignment would otherwise require materialized as its own synthetic
// `[N x i8]` gap member, rather than left to an implicit, non-packed
// struct layout -- see `layOutStructIfOffsetsMatch`'s own comment for why
// (a `DataLayout`-timing bug that silently baked in the wrong byte offset
// for a member after such a gap). This particular struct needs a
// trailing 12-byte pad (rounding its own size up to its largest member's
// 16-byte alignment), but no interior one (`f32` immediately follows
// `vector<4xf32>` at an already-naturally-aligned offset).

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gl_PerVertex :
      !spirv.ptr<!spirv.struct<(vector<4xf32> [BuiltIn=0 : i32],
                               f32 [BuiltIn=1 : i32])>, Output>
}

// CHECK: @gl_PerVertex = external addrspace(8) global <{ <4 x float>, float, [12 x i8] }>, !feme.spirv.MemberDecorations ![[MEMBERS:[0-9]+]]
// CHECK-DAG: ![[MEMBERS]] = !{![[M0:[0-9]+]], ![[M1:[0-9]+]]}
// CHECK-DAG: ![[M0]] = !{i32 0, ![[M0DECOS:[0-9]+]]}
// CHECK-DAG: ![[M0DECOS]] = !{![[M0BUILTIN:[0-9]+]]}
// CHECK-DAG: ![[M0BUILTIN]] = !{i32 11, i32 0}
// CHECK-DAG: ![[M1]] = !{i32 1, ![[M1DECOS:[0-9]+]]}
// CHECK-DAG: ![[M1DECOS]] = !{![[M1BUILTIN:[0-9]+]]}
// CHECK-DAG: ![[M1BUILTIN]] = !{i32 11, i32 1}
