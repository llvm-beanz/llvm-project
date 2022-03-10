// RUN: %clang -target dxil-pc-sm6.0-pixel -x hlsl -S -emit-llvm -o - %s | FileCheck %s
// RUN: %clang -target dxil-pc-sm6.0-vertex -x hlsl -S -emit-llvm -o - %s | FileCheck %s
// RUN: %clang -target dxil-pc-sm6.0-compute -x hlsl -S -emit-llvm -o - %s | FileCheck %s
// RUN: %clang -target dxil-pc-sm6.0-lib -x hlsl -S -emit-llvm -o - %s | FileCheck %s
// RUN: %clang -target dxil-pc-sm6.0-hull -x hlsl -S -emit-llvm -o - %s | FileCheck %s
// RUN: %clang -target dxil-pc-sm6.0-domain -x hlsl -S -emit-llvm -o - %s | FileCheck %s
// RUN: %clang -target dxil-pc-sm6.0-geometry -x hlsl -S -emit-llvm -o - %s | FileCheck %s

// CHECK: target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
// CHECK: target triple = "dxil-pc-sm6.0-{{[a-z]+}}"
