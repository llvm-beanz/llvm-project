// RUN: %clang_cc1 -fno-switch -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

struct Condition {
  Condition();
  ~Condition();
  operator int() const;
};

int condition_cleanup(int value) {
  int result = 0;
  switch (Condition condition{}; value) {
  case 1:
    result = 1;
    break;
  case 2:
    result = 2;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 @_Z17condition_cleanupi(
// CHECK-NOT: switch
// CHECK: br i1
// CHECK: sw.bb:
// CHECK: br label %cleanup
// CHECK: sw.next2:
// CHECK: br label %cleanup
// CHECK: cleanup:
// CHECK: call void @_ZN9ConditionD1Ev
