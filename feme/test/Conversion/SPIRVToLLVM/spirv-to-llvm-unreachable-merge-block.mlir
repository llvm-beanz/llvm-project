// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that a structured-selection merge block with no predecessors at
// all -- because *every* arm of the selection instead ends in a real
// terminator of its own (`spirv.EXT.EmitMeshTasks`, which ends the whole
// invocation) rather than branching to the merge block SPIR-V's own
// structured-CFG rules otherwise require -- converts cleanly, rather than
// leaving its `spirv.Unreachable` stranded in the `spirv` dialect
// (roadmap H81). `glslangValidator`'s own code generation for
// `dEQP-VK.mesh_shader.ext.misc.emit_in_control_flow`'s task shader (an
// `if (...) { EmitMeshTasksEXT(...); } else { EmitMeshTasksEXT(...); }`
// with no code after it) hits exactly this shape: it emits the
// structurally-required merge block, terminated with `OpUnreachable`
// since nothing can ever actually reach it. MLIR's own SPIR-V
// deserializer imports that faithfully as a block with zero predecessors
// holding `spirv.Unreachable`. `applyPartialConversion`'s conversion
// driver only legalizes ops reachable from a region's own entry block (by
// design -- converting dead code is wasted work), so it silently leaves
// such an orphaned block's `spirv.Unreachable` unconverted, which then
// fails outright at the final `translateModuleToLLVMIR` step with
// "missing `LLVMTranslationDialectInterface` registration ... for op:
// spirv.Unreachable" (the `spirv` dialect has no such interface
// registered at all: every `spirv` op is expected to have already
// converted to the `llvm` dialect by that point). The fix erases every
// block genuinely unreachable from its function's own entry block before
// the conversion runs, so no dead `spirv` op is ever left over for the
// conversion driver to skip.

// CHECK-LABEL: llvm.func @task_entry
// CHECK: llvm.cond_br %{{.*}}, ^[[THEN:.*]], ^[[ELSE:.*]]
// CHECK: ^[[THEN]]:
// CHECK: llvm.call @feme.stage.emit_mesh_tasks
// CHECK-NEXT: llvm.return
// CHECK: ^[[ELSE]]:
// CHECK: llvm.call @feme.stage.emit_mesh_tasks
// CHECK-NEXT: llvm.return
// CHECK-NOT: spirv.Unreachable
// CHECK-NOT: llvm.unreachable
spirv.module Logical GLSL450 requires #spirv.vce<v1.4, [MeshShadingEXT], [SPV_EXT_mesh_shader]> {
  spirv.func @task_entry() "None" {
    %cond = spirv.Constant true
    spirv.BranchConditional %cond, ^then, ^else
  ^then:
    %x0 = spirv.Constant 2 : i32
    %y0 = spirv.Constant 1 : i32
    %z0 = spirv.Constant 1 : i32
    spirv.EXT.EmitMeshTasks %x0, %y0, %z0 : i32, i32, i32
  ^else:
    %x1 = spirv.Constant 1 : i32
    %y1 = spirv.Constant 1 : i32
    %z1 = spirv.Constant 1 : i32
    spirv.EXT.EmitMeshTasks %x1, %y1, %z1 : i32, i32, i32
  ^merge:
    spirv.Unreachable
  }
  spirv.EntryPoint "TaskEXT" @task_entry
  spirv.ExecutionMode @task_entry "LocalSize", 1, 1, 1
}
