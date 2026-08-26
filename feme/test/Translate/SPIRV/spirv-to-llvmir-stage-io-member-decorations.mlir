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

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gl_PerVertex :
      !spirv.ptr<!spirv.struct<(vector<4xf32> [BuiltIn=0 : i32],
                               f32 [BuiltIn=1 : i32])>, Output>
}

// CHECK: @gl_PerVertex = external addrspace(8) global { <4 x float>, float }, !feme.spirv.MemberDecorations ![[MEMBERS:[0-9]+]]
// CHECK-DAG: ![[MEMBERS]] = !{![[M0:[0-9]+]], ![[M1:[0-9]+]]}
// CHECK-DAG: ![[M0]] = !{i32 0, ![[M0DECOS:[0-9]+]]}
// CHECK-DAG: ![[M0DECOS]] = !{![[M0BUILTIN:[0-9]+]]}
// CHECK-DAG: ![[M0BUILTIN]] = !{i32 11, i32 0}
// CHECK-DAG: ![[M1]] = !{i32 1, ![[M1DECOS:[0-9]+]]}
// CHECK-DAG: ![[M1DECOS]] = !{![[M1BUILTIN:[0-9]+]]}
// CHECK-DAG: ![[M1BUILTIN]] = !{i32 11, i32 1}
