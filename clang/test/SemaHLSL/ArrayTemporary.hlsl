// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.3-library -ast-dump %s | Filecheck %s

void fn(float x[2]) { }

// CHECK: CallExpr {{.*}} 'void'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'void (*)(float[2])' <FunctionToPointerDecay>
// CHECK-NEXT: DeclRefExpr {{.*}} 'void (float[2])' lvalue Function {{.*}} 'fn' 'void (float[2])'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'float[2]' <ArrayParamNoOp>

void call() {
  float Arr[2] = {0, 0};
  fn(Arr);
}

struct Obj {
  float V;
  int X;
};

void fn2(Obj O[4]) { }

// CHECK: CallExpr {{.*}} 'void'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'void (*)(Obj[4])' <FunctionToPointerDecay>
// CHECK-NEXT: DeclRefExpr {{.*}} 'void (Obj[4])' lvalue Function {{.*}} 'fn2' 'void (Obj[4])'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'Obj[4]' <ArrayParamNoOp>

void call2() {
  Obj Arr[4] = {};
  fn2(Arr);
}


void fn3(float x[2][2]) { }

// CHECK: CallExpr {{.*}} 'void'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'void (*)(float[2][2])' <FunctionToPointerDecay>
// CHECK-NEXT: DeclRefExpr {{.*}} 'void (float[2][2])' lvalue Function {{.*}} 'fn3' 'void (float[2][2])'
// CHECK-NEXT: ImplicitCastExpr {{.*}} 'float[2][2]' <ArrayParamNoOp>

void call3() {
  float Arr[2][2] = {{0, 0}, {1,1}};
  fn3(Arr);
}
