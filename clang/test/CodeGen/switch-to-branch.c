// RUN: %clang_cc1 -fno-switch -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s
// RUN: %clang_cc1 -fswitch -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=SWITCH
// RUN: %clang_cc1 -fno-switch -fprofile-instrument=clang -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=PROFILE

int fallthrough(int value) {
  int result = 0;
  switch (value) {
  case 0:
    result += 1;
  default:
    result += 2;
  case 2:
  case 3:
    result += 4;
    break;
  case 10 ... 20:
    result += 8;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 @fallthrough(
// CHECK-NOT: switch
// CHECK: icmp eq i32 %{{.*}}, 0
// CHECK: br i1 %{{.*}}, label %[[CASE0:sw.bb[0-9]*]], label %[[NEXT:sw.next[0-9]*]]
// CHECK: [[CASE0]]:
// CHECK: br label %[[NEXT]]
// CHECK: [[NEXT]]:{{.*}}preds = %[[CASE0]], %sw.dispatch
// CHECK: phi i1 [ true, %[[CASE0]] ], [ false, %sw.dispatch ]
// CHECK: icmp ule i32 %{{.*}}, 10
// CHECK: icmp eq i32 %{{.*}}, 3
// CHECK: icmp eq i32 %{{.*}}, 2
// CHECK: icmp eq i32 %{{.*}}, 0
// CHECK: br i1 %{{.*}}, label %[[DEFAULT:sw.bb[0-9]+]], label %[[NEXT2:sw.next[0-9]+]]
// CHECK: [[DEFAULT]]:
// CHECK: br label %[[NEXT2]]
// CHECK: [[NEXT2]]:{{.*}}preds = %[[DEFAULT]], %[[NEXT]]
// CHECK: phi i1 [ true, %[[DEFAULT]] ], [ false, %[[NEXT]] ]
// CHECK: icmp eq i32 %{{.*}}, 2
// CHECK: br i1 %{{.*}}, label %[[CASE2:sw.bb[0-9]+]], label %[[NEXT3:sw.next[0-9]+]]
// CHECK: [[CASE2]]:
// CHECK: br label %[[NEXT3]]
// CHECK: [[NEXT3]]:{{.*}}preds = %[[CASE2]], %[[NEXT2]]
// CHECK: phi i1 [ true, %[[CASE2]] ], [ false, %[[NEXT2]] ]
// CHECK: icmp eq i32 %{{.*}}, 3
// CHECK: br i1 %{{.*}}, label %[[CASE3:sw.bb[0-9]+]], label %[[NEXT4:sw.next[0-9]+]]
// CHECK: [[CASE3]]:
// CHECK: br label %sw.epilog
// CHECK: [[NEXT4]]:
// CHECK: icmp ule i32 %{{.*}}, 10
// CHECK: ret i32

// SWITCH-LABEL: define{{.*}} i32 @fallthrough(
// SWITCH: switch i32

// PROFILE-LABEL: define{{.*}} i32 @fallthrough(
// PROFILE: sw.bb:
// PROFILE-NEXT: call void @llvm.instrprof.increment
// PROFILE: br label %skipcount
// PROFILE: sw.bb1:
// PROFILE-NEXT: call void @llvm.instrprof.increment
// PROFILE: br label %skipcount
// PROFILE: skipcount:{{.*}}preds = %sw.bb1, %sw.bb

int nested(int outer, int inner) {
  switch (outer) {
  case 1:
    switch (inner) {
    case 2:
      return 3;
    default:
      return 4;
    }
  default:
    return 5;
  }
}

// CHECK-LABEL: define{{.*}} i32 @nested(
// CHECK-NOT: switch
// CHECK-COUNT-4: br i1

int no_labels(int value) {
  switch (value) {
    value++;
  }
  return value;
}

// CHECK-LABEL: define{{.*}} i32 @no_labels(
// CHECK-NOT: switch
// CHECK: ret i32

int nested_case(int value, int count) {
  switch (value) {
    while (count--) {
    case 1:
      value++;
    }
  }
  return value;
}

// CHECK-LABEL: define{{.*}} i32 @nested_case(
// CHECK-NOT: switch
// CHECK: phi i1
// CHECK: ret i32
