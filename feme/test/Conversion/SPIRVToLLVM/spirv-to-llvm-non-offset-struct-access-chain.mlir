// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap H96: `OffsetStructMemberReorderAccessChainPattern` (added for
// Roadmap H101p) used to call `getOffsetSortedMemberIndices`/
// `structHasLeadingOffsetPad` on *every* struct-typed `spirv.AccessChain`
// pointee, including an ordinary struct with no member `Offset`
// decorations at all (e.g. an ordinary `Function`-storage-class local
// variable's type, as below, which is never `Block`-decorated and never
// carries per-member byte offsets the way a uniform/storage-buffer
// interface block does). Since `StructType::getMemberOffset` dereferences
// a null offset-info array for such a struct, this crashed with a bare
// `SIGSEGV` (not an assertion failure) the moment this pattern's own
// `matchAndRewrite` was invoked on one -- rare enough in most caselists
// that it went unnoticed until roughly 2,000-2,500 cases into a `deqp-vk`
// run before the first such struct was reached, misleadingly presenting
// as a slow, gradual resource-exhaustion bug rather than the deterministic,
// shape-triggered null-pointer read it actually is. The fix guards both
// helpers behind `StructType::hasOffset()`, matching every other call site
// of `getOffsetSortedMemberIndices` in this file.
//
// CHECK-LABEL: llvm.func @local_struct
// CHECK: %[[VAR:.*]] = llvm.alloca %{{.*}} x !llvm.struct<(i32, i32)>
// CHECK: %[[ELEM:.*]] = llvm.getelementptr %[[VAR]][%{{.*}}, 1]
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : i32, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.func @local_struct(%val : si32) -> () "None" {
    %var = spirv.Variable : !spirv.ptr<!spirv.struct<Pair, (si32, si32)>, Function>
    %c1 = spirv.Constant 1 : si32
    %elem = spirv.AccessChain %var[%c1] : !spirv.ptr<!spirv.struct<Pair, (si32, si32)>, Function>, si32 -> !spirv.ptr<si32, Function>
    spirv.Store "Function" %elem, %val : si32
    spirv.Return
  }
}
