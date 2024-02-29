// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.3-library -emit-llvm -disable-llvm-passes -o - %s | Filecheck %s

void fn(float x[2]) { }

// CHECK-LABEL: define void {{.*}}call{{.*}}
// CHECK: [[Arr:%.*]] = alloca [2 x float]
// CHECK: [[Tmp:%.*]] = alloca [2 x float]
// CHECK: call void @llvm.memset.p0.i32(ptr align 4 [[Arr]], i8 0, i32 8, i1 false)
// CHECK: call void @llvm.memcpy.p0.p0.i32(ptr align 4 [[Tmp]], ptr align 4 [[Arr]], i32 8, i1 false)
// CHECK: [[Decay:%.*]] = getelementptr inbounds [2 x float], ptr [[Tmp]], i32 0, i32 0
// CHECK: call void {{.*}}fn{{.*}}(ptr noundef [[Decay]])
void call() {
  float Arr[2] = {0, 0};
  fn(Arr);
}

struct Obj {
  float V;
  int X;
};

void fn2(Obj O[4]) { }

// CHECK-LABEL: define void {{.*}}call2{{.*}}
// CHECK: [[Arr:%.*]] = alloca [4 x %struct.Obj]
// CHECK: [[Tmp:%.*]] = alloca [4 x %struct.Obj]
// CHECK: call void @llvm.memset.p0.i32(ptr align 4 [[Arr]], i8 0, i32 32, i1 false)
// CHECK: call void @llvm.memcpy.p0.p0.i32(ptr align 4 [[Tmp]], ptr align 4 [[Arr]], i32 32, i1 false)
// CHECK: [[Decay:%.*]] = getelementptr inbounds [4 x %struct.Obj], ptr [[Tmp]], i32 0, i32 0
// CHECK: call void {{.*}}fn2{{.*}}(ptr noundef [[Decay]])
void call2() {
  Obj Arr[4] = {};
  fn2(Arr);
}


void fn3(float x[2][2]) { }

// CHECK-LABEL: define void {{.*}}call3{{.*}}
// CHECK: [[Arr:%.*]] = alloca [2 x [2 x float]]
// CHECK: [[Tmp:%.*]] = alloca [2 x [2 x float]]
// CHECK: call void @llvm.memcpy.p0.p0.i32(ptr align 4 [[Arr]], ptr align 4 {{.*}}, i32 16, i1 false)
// CHECK: call void @llvm.memcpy.p0.p0.i32(ptr align 4 [[Tmp]], ptr align 4 [[Arr]], i32 16, i1 false)
// CHECK: [[Decay:%.*]] = getelementptr inbounds [2 x [2 x float]], ptr [[Tmp]], i32 0, i32 0
// CHECK: call void {{.*}}fn3{{.*}}(ptr noundef [[Decay]])
void call3() {
  float Arr[2][2] = {{0, 0}, {1,1}};
  fn3(Arr);
}
