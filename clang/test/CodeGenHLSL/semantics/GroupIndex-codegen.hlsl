// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-compute -x hlsl -emit-llvm -disable-llvm-passes -o - %s

[numthreads(1,1,1)]
void main(unsigned GI : SV_GroupIndex) {
}

// CHECK: %GI.addr = alloca i32, align 4
// CHECK-NEXT: %0 = call i32 @llvm.dx.flattened.thread.id.in.group()
// CHECK-NEXT: store i32 %0, ptr %GI.addr, align 4
