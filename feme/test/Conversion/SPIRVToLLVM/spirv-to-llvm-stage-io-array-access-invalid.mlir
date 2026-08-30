// RUN: feme-opt --feme-convert-spirv-to-llvm --verify-diagnostics --split-input-file %s

// (Roadmap H7x, combined with H7w's own already-tracked dynamic-index
// gap) A *non-constant* index into a value-modeled stage-IO `Input` array
// (`gl_ClipDistance`/`gl_CullDistance` read from a fragment stage) has no
// `llvm.extractvalue` representation at all -- unlike `getelementptr`,
// `llvm.extractvalue`'s own index operands are compile-time-constant
// only, so `StageIOArrayAccessChainPattern` declines to match this shape
// (see its own comment) and MLIR's own generic, pointer-assuming
// `spirv.AccessChain` pattern is tried instead, which -- since the base
// operand converted to a non-pointer array value, not a real pointer --
// produces an `llvm.getelementptr` that fails verification with a clean,
// diagnosed error rather than crashing.

spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader, ClipDistance], []> {
  spirv.GlobalVariable @gl_ClipDistance built_in("ClipDistance") : !spirv.ptr<!spirv.array<2 x f32>, Input>
  spirv.func @read_clip_distance_dynamic(%idx : i32) -> f32 "None" {
    %0 = spirv.mlir.addressof @gl_ClipDistance : !spirv.ptr<!spirv.array<2 x f32>, Input>
    // expected-error@+1 {{'llvm.getelementptr' op operand #0 must be LLVM pointer type or LLVM dialect-compatible vector of LLVM pointer type, but got '!llvm.array<2 x f32>'}}
    %ac = spirv.AccessChain %0[%idx] : !spirv.ptr<!spirv.array<2 x f32>, Input>, i32 -> !spirv.ptr<f32, Input>
    %v = spirv.Load "Input" %ac : f32
    spirv.ReturnValue %v : f32
  }
}
