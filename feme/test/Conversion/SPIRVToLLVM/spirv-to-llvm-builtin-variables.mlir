// RUN: feme-opt --feme-convert-spirv-to-llvm --split-input-file %s | FileCheck %s

// Checks that SPIR-V builtin input variables convert into the `llvm.spv.*`
// intrinsics LLVM's SPIRV backend reads the same values with, rather than
// into loads from an LLVM global nothing ever defines (see the "Known gap"
// note in the SPIR-V section of feme/docs/Design.md).

// A vector-valued builtin is read one component at a time: the intrinsics are
// scalar, and take the component index.

// CHECK-NOT: llvm.mlir.global{{.*}}@gid
// CHECK-LABEL: llvm.func @read_global_invocation_id
// CHECK: %[[POISON:.*]] = llvm.mlir.poison : vector<3xi32>
// CHECK: %[[I0:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[X:.*]] = llvm.call_intrinsic "llvm.spv.thread.id"(%[[I0]])
// CHECK: %[[V0:.*]] = llvm.insertelement %[[X]], %[[POISON]][%[[I0]] : i32]
// CHECK: %[[I1:.*]] = llvm.mlir.constant(1 : i32) : i32
// CHECK: %[[Y:.*]] = llvm.call_intrinsic "llvm.spv.thread.id"(%[[I1]])
// CHECK: %[[V1:.*]] = llvm.insertelement %[[Y]], %[[V0]][%[[I1]] : i32]
// CHECK: %[[I2:.*]] = llvm.mlir.constant(2 : i32) : i32
// CHECK: %[[Z:.*]] = llvm.call_intrinsic "llvm.spv.thread.id"(%[[I2]])
// CHECK: %[[V2:.*]] = llvm.insertelement %[[Z]], %[[V1]][%[[I2]] : i32]
// CHECK: llvm.return %[[V2]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.func @read_global_invocation_id() -> vector<3xi32> "None" {
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    spirv.ReturnValue %1 : vector<3xi32>
  }
}

// -----

// An `spirv.AccessChain` selecting a single lane of a builtin `Input`
// vector (e.g. `gl_GlobalInvocationID.x`) -- the shape glslang emits when
// only one component is ever read -- converts directly to an
// `llvm.extractelement` on the intrinsic-built vector, rather than falling
// through to MLIR's own `spirv.AccessChain` pattern, which would otherwise
// try to GEP into that vector value as if it were a real pointer.

// CHECK-LABEL: llvm.func @read_global_invocation_id_x
// CHECK: %[[XI:.*]] = llvm.call_intrinsic "llvm.spv.thread.id"
// CHECK: %[[XVEC:.*]] = llvm.insertelement %[[XI]]
// CHECK: llvm.insertelement %{{.*}}, %[[XVEC]]
// CHECK: %[[FULLVEC:.*]] = llvm.insertelement %{{.*}}, %{{.*}}
// CHECK: %[[LANEIDX:.*]] = llvm.mlir.constant(0 : i32) : i32
// CHECK: %[[LANE:.*]] = llvm.extractelement %[[FULLVEC]][%[[LANEIDX]] : i32] : vector<3xi32>
// CHECK: llvm.return %[[LANE]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @gid built_in("GlobalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.func @read_global_invocation_id_x() -> i32 "None" {
    %0 = spirv.mlir.addressof @gid : !spirv.ptr<vector<3xi32>, Input>
    %c0 = spirv.Constant 0 : i32
    %1 = spirv.AccessChain %0[%c0] : !spirv.ptr<vector<3xi32>, Input>, i32 -> !spirv.ptr<i32, Input>
    %2 = spirv.Load "Input" %1 : i32
    spirv.ReturnValue %2 : i32
  }
}

// -----

// A scalar builtin whose intrinsic takes no operand at all.

// CHECK-LABEL: llvm.func @read_local_invocation_index
// CHECK: %[[V:.*]] = llvm.call_intrinsic "llvm.spv.flattened.thread.id.in.group"() : () -> i32
// CHECK: llvm.return %[[V]]
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @idx built_in("LocalInvocationIndex") : !spirv.ptr<i32, Input>
  spirv.func @read_local_invocation_index() -> i32 "None" {
    %0 = spirv.mlir.addressof @idx : !spirv.ptr<i32, Input>
    %1 = spirv.Load "Input" %0 : i32
    spirv.ReturnValue %1 : i32
  }
}

// -----

// The remaining compute builtins map onto their own intrinsic families.

// CHECK-LABEL: llvm.func @read_the_rest
// CHECK: llvm.call_intrinsic "llvm.spv.group.id"
// CHECK: llvm.call_intrinsic "llvm.spv.thread.id.in.group"
// CHECK: llvm.call_intrinsic "llvm.spv.num.workgroups"
spirv.module Logical GLSL450 requires #spirv.vce<v1.0, [Shader], []> {
  spirv.GlobalVariable @group built_in("WorkgroupId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @local built_in("LocalInvocationId") : !spirv.ptr<vector<3xi32>, Input>
  spirv.GlobalVariable @count built_in("NumWorkgroups") : !spirv.ptr<vector<3xi32>, Input>
  spirv.func @read_the_rest() -> vector<3xi32> "None" {
    %0 = spirv.mlir.addressof @group : !spirv.ptr<vector<3xi32>, Input>
    %1 = spirv.Load "Input" %0 : vector<3xi32>
    %2 = spirv.mlir.addressof @local : !spirv.ptr<vector<3xi32>, Input>
    %3 = spirv.Load "Input" %2 : vector<3xi32>
    %4 = spirv.mlir.addressof @count : !spirv.ptr<vector<3xi32>, Input>
    %5 = spirv.Load "Input" %4 : vector<3xi32>
    %6 = spirv.IAdd %1, %3 : vector<3xi32>
    %7 = spirv.IAdd %6, %5 : vector<3xi32>
    spirv.ReturnValue %7 : vector<3xi32>
  }
}
