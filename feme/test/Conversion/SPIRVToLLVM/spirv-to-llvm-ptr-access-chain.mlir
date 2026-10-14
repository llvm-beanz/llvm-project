// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Roadmap K6a: `spirv.PtrAccessChain`/`spirv.InBoundsPtrAccessChain` had no
// conversion pattern at all (in either feme or upstream MLIR), failing
// legalization with "explicitly marked illegal" on every
// `dEQP-VK.spirv_assembly.instruction.*variable_pointer*` case that
// lowered one (156 of the `variablePointers`/`variablePointersStorageBuffer`
// feature bits' own 296 mustpass failures). `PtrAccessChainPattern`
// (SPIRVToLLVMPatterns.cpp) covers every shape the cluster actually
// exercises: a compile-time-constant-zero `element` operand alongside one
// or more real trailing indices (re-expressed as a plain `AccessChain` and
// reconverted by this file's own existing, much more intricate
// `AccessChain` pattern set), a genuinely dynamic `element` operand with
// no trailing indices into a plain scalar pointee (lowered directly to a
// single `llvm.getelementptr`, matching C's own `scalarPtr[i]` pointer
// arithmetic), and a non-zero/dynamic `element` *together with* real
// trailing indices into a non-struct (scalar/vector) pointee (lowered to
// a chained pair of `llvm.getelementptr`s). A non-zero `element` combined
// with further indices into a *struct* pointee is not covered (no known
// caller needs it) and is left to decline legalization rather than
// miscompile.

// A zero `element` with one trailing (struct-member-selecting) index is
// reconverted as a plain `AccessChain`, letting the already-correct
// struct-layout machinery elsewhere in this file build the final GEP.
//
// CHECK-LABEL: llvm.func @ptr_access_chain_zero_element
// CHECK: %[[VAR:.*]] = llvm.alloca %{{.*}} x !llvm.struct<packed (i32, i32)>
// CHECK: %[[ELEM:.*]] = llvm.getelementptr %[[VAR]][%{{.*}}, 1]
// CHECK: llvm.store %{{.*}}, %[[ELEM]] : i32, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader, VariablePointers], [SPV_KHR_variable_pointers]> {
  spirv.func @ptr_access_chain_zero_element(%val : si32) -> () "None" {
    %var = spirv.Variable : !spirv.ptr<!spirv.struct<Pair, (si32, si32)>, Function>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %elem = spirv.PtrAccessChain %var[%c0 %c1] : !spirv.ptr<!spirv.struct<Pair, (si32, si32)>, Function>, si32, si32 -> !spirv.ptr<si32, Function>
    spirv.Store "Function" %elem, %val : si32
    spirv.Return
  }
}

// -----

// A zero `element` with no trailing indices at all is the identity --
// the "This computed Base has the same type as the originating Base"
// case with nothing further to apply -- and is replaced with the base
// pointer directly, with no new instruction emitted for the access chain
// itself.
//
// CHECK-LABEL: llvm.func @ptr_access_chain_zero_element_identity
// CHECK: %[[VAR:.*]] = llvm.alloca
// CHECK-NOT: llvm.getelementptr
// CHECK: llvm.store %{{.*}}, %[[VAR]] : f32, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader, VariablePointers], [SPV_KHR_variable_pointers]> {
  spirv.func @ptr_access_chain_zero_element_identity(%val : f32) -> () "None" {
    %var = spirv.Variable : !spirv.ptr<f32, Function>
    %c0 = spirv.Constant 0 : si32
    %elem = spirv.PtrAccessChain %var[%c0] : !spirv.ptr<f32, Function>, si32 -> !spirv.ptr<f32, Function>
    spirv.Store "Function" %elem, %val : f32
    spirv.Return
  }
}

// -----

// A dynamic `element` with no trailing indices into a plain scalar
// pointee lowers directly to a single `llvm.getelementptr`, matching C's
// own `scalarPtr[i]` pointer arithmetic (the shape
// `dEQP-VK.spirv_assembly.instruction.compute.variable_pointers.compute.
// reads_opptraccesschain_*`'s own `float`-pointee family uses, built from
// a genuinely runtime-varying index rather than a compile-time constant).
//
// CHECK-LABEL: llvm.func @ptr_access_chain_dynamic_element
// CHECK: %[[VAR:.*]] = llvm.alloca
// CHECK: %[[PTR:.*]] = llvm.getelementptr %[[VAR]][%{{.*}}] : (!llvm.ptr, i32) -> !llvm.ptr, f32
// CHECK: llvm.store %{{.*}}, %[[PTR]] : f32, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader, VariablePointers], [SPV_KHR_variable_pointers]> {
  spirv.func @ptr_access_chain_dynamic_element(%val : f32, %idx : si32) -> () "None" {
    %var = spirv.Variable : !spirv.ptr<f32, Function>
    %elem = spirv.PtrAccessChain %var[%idx] : !spirv.ptr<f32, Function>, si32 -> !spirv.ptr<f32, Function>
    spirv.Store "Function" %elem, %val : f32
    spirv.Return
  }
}

// -----

// A non-zero `element` *together with* a trailing index -- the
// `opptraccesschain_float_*` family's own
// `%ptr<v4f32> -> (element)(idx) -> %ptr<f32>` shape: step to vector lane
// `element`'s own base address, then select component `idx` within it --
// lowers to a chained pair of `llvm.getelementptr`s rather than
// re-expressing either step as a new SPIR-V op for the dialect conversion
// driver to reprocess (an op-reprocessing attempt at this same shape
// previously failed to converge: a newly created `AccessChain`'s
// legalization ran before its own `PtrAccessChain` operand had converted,
// so its failure was never retried).
//
// CHECK-LABEL: llvm.func @ptr_access_chain_element_and_index
// CHECK: %[[VAR:.*]] = llvm.alloca
// CHECK: %[[STEP:.*]] = llvm.getelementptr %[[VAR]][%{{.*}}] : (!llvm.ptr, i32) -> !llvm.ptr, vector<4xf32>
// CHECK: %[[ZERO:.*]] = llvm.mlir.constant(0 : i32)
// CHECK: %[[PTR:.*]] = llvm.getelementptr %[[STEP]][%[[ZERO]], %{{.*}}] : (!llvm.ptr, i32, i32) -> !llvm.ptr, vector<4xf32>
// CHECK: llvm.store %{{.*}}, %[[PTR]] : f32, !llvm.ptr
spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader, VariablePointers], [SPV_KHR_variable_pointers]> {
  spirv.func @ptr_access_chain_element_and_index(%val : f32, %element : si32, %idx : si32) -> () "None" {
    %var = spirv.Variable : !spirv.ptr<vector<4xf32>, Function>
    %elem = spirv.PtrAccessChain %var[%element %idx] : !spirv.ptr<vector<4xf32>, Function>, si32, si32 -> !spirv.ptr<f32, Function>
    spirv.Store "Function" %elem, %val : f32
    spirv.Return
  }
}

