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

int nested_cleanup(int outer, int inner) {
  int result = 0;
  switch (Condition outer_guard{}; outer) {
  case 0:
  case 1:
    switch (Condition inner_guard{}; inner) {
    case 0:
      result += 1;
    case 1:
      result += 2;
      break;
    default:
      result += 4;
    }
  case 2:
    result += 8;
    break;
  default:
    result += 16;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 @_Z14nested_cleanupii(
// CHECK: call void @_ZN9ConditionC1Ev(ptr {{[^,]*}}%outer_guard)
// CHECK: br label %sw.dispatch
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: call void @_ZN9ConditionC1Ev(ptr {{[^,]*}}%inner_guard)
// CHECK: add nsw i32 %{{.*}}, 1
// CHECK: phi i1
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %[[CLEANUP:cleanup[0-9]*]]
// CHECK: {{^}}[[CLEANUP]]:
// CHECK-NEXT: call void @_ZN9ConditionD1Ev(ptr {{[^,]*}}%inner_guard)
// CHECK: [[INNER_EXIT:sw.epilog[0-9]*]]:
// CHECK-NEXT: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %[[INNER_EXIT]] ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 8
// CHECK: br label %[[OUTER_CLEANUP:cleanup[0-9]*]]
// CHECK: {{^}}[[OUTER_CLEANUP]]:
// CHECK-NEXT: call void @_ZN9ConditionD1Ev(ptr {{[^,]*}}%outer_guard)
// CHECK: ret i32
