// Source text for seed-corpus/loop-merge-phi.spv (see ../seed-corpus in
// feme-spirv-import-fuzzer.md for regeneration instructions). Unstructured
// control flow: a loop whose merge block takes a block argument fed by two
// predecessors -- the header's loop-exit edge and an early-exit edge from
// inside the loop body -- exactly the `OpPhi`-in-loop-merge-block shape
// real compiler output uses for a loop with a value-producing `break`
// (see "SPIR-V import prerequisites" in
// ../../../docs/FeMeVulkanDesign.md and roadmap milestone V0.5). MLIR's
// structured deserializer rejects this shape outright; feme::SPIRVImporter's
// default unstructured deserialization -- exercised here -- handles it the
// same as any other CFG, giving the fuzzer a seed shaped like the real
// shaders that motivated this milestone instead of only trivial,
// branch-free ones.
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gv : !spirv.ptr<i32, Private>
  spirv.func @main() -> () "None" {
    %init = spirv.Constant 0 : i32
    spirv.Branch ^header(%init : i32)
  ^header(%i: i32):
    %bound = spirv.Constant 10 : i32
    %cond = spirv.SLessThan %i, %bound : i32
    spirv.BranchConditional %cond, ^body, ^merge(%i : i32)
  ^body:
    %five = spirv.Constant 5 : i32
    %isFive = spirv.IEqual %i, %five : i32
    spirv.BranchConditional %isFive, ^merge(%i : i32), ^latch
  ^latch:
    %one = spirv.Constant 1 : i32
    %next = spirv.IAdd %i, %one : i32
    spirv.Branch ^header(%next : i32)
  ^merge(%result: i32):
    %addr = spirv.mlir.addressof @gv : !spirv.ptr<i32, Private>
    spirv.Store "Private" %addr, %result : i32
    spirv.Return
  }
  spirv.EntryPoint "GLCompute" @main
  spirv.ExecutionMode @main "LocalSize", 1, 1, 1
}
