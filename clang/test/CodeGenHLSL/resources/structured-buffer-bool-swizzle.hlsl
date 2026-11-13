// REQUIRES: spirv-registered-target
// RUN: %clang_cc1 -triple spirv-unknown-vulkan-compute -finclude-default-header -hlsl-entry main -O3 -emit-llvm -o - %s | FileCheck %s --check-prefixes=CHECK,SPIRV
// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.5-compute -finclude-default-header -hlsl-entry main -O3 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple spirv-unknown-vulkan-compute -finclude-default-header -hlsl-entry main -O3 -emit-obj -o %t %s

StructuredBuffer<bool4> In : register(t0);
RWStructuredBuffer<bool> Out : register(u1);

[numthreads(1, 1, 1)]
void main() {
  Out[0] = all(In[0]);
  Out[1] = any(In[1].xyz);
  Out[2] = all(In[1].w);
  Out[3] = any(In[2].xy);
  Out[4] = all(In[2].zw);
}

// HLSL buffer bools are true for any nonzero storage value. In particular,
// loading a scalar swizzle must not truncate an i32 into an i1. InstCombine
// can turn such a truncation into a bitcast to a 128-element boolean vector,
// which cannot be represented by SPIR-V's OpBitcast.
// CHECK-LABEL: define void @main()
// CHECK: %[[ALL4:.*]] = icmp eq <4 x i32> {{.*}}, zeroinitializer
// CHECK-NEXT: %[[ALL4BITS:.*]] = bitcast <4 x i1> %[[ALL4]] to i4
// CHECK-NEXT: icmp eq i4 %[[ALL4BITS]], 0
// CHECK: icmp ne <3 x i32> {{.*}}, zeroinitializer
// SPIRV: extractelement <4 x i32> {{.*}}, i64 3
// CHECK: icmp ne i32 {{.*}}, 0
// CHECK: icmp ne i64 {{.*}}, 0
// CHECK: %[[ALL2:.*]] = icmp eq <2 x i32> {{.*}}, zeroinitializer
// CHECK-NEXT: %[[ALL2BITS:.*]] = bitcast <2 x i1> %[[ALL2]] to i2
// CHECK-NEXT: icmp eq i2 %[[ALL2BITS]], 0
