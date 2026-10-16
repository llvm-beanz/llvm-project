// RUN: %clang_cc1 -fno-switch -emit-llvm -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --implicit-check-not='switch i'
// RUN: %clang_cc1 -fswitch -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=SWITCH
// RUN: %clang_cc1 -fno-switch -fprofile-instrument=clang -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=PROFILE
// RUN: %clang_cc1 -fno-switch -ftrivial-auto-var-init=zero -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=INIT -DVALUE=0
// RUN: %clang_cc1 -fno-switch -ftrivial-auto-var-init=pattern -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=INIT -DVALUE=-1431655766

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

int case_in_for(int value, int count) {
  switch (value) {
    for (; count > 0; --count) {
    case 0:
    case 1:
      value += 4;
    }
  case 2:
    value += 8;
  }
  return value;
}

// CHECK-LABEL: define{{.*}} i32 @case_in_for(
// CHECK: for.body:
// CHECK-NEXT: br label %sw.dispatch
// CHECK: sw.dispatch:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %for.body ], [ false, %entry ]
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: add nsw i32 %{{.*}}, 4
// CHECK: br label %for.inc
// CHECK: for.end:
// CHECK-NEXT: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %for.end ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 8

int case_in_if(int value, int condition) {
  switch (value) {
    if (condition) {
    case 0:
    case 1:
      value += 4;
    }
  case 2:
    value += 8;
  }
  return value;
}

// CHECK-LABEL: define{{.*}} i32 @case_in_if(
// CHECK: if.then:
// CHECK-NEXT: br label %sw.dispatch
// CHECK: sw.dispatch:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %if.then ], [ false, %entry ]
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: add nsw i32 %{{.*}}, 4
// CHECK: br label %if.end
// CHECK: if.end:
// CHECK-NEXT: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %if.end ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 8

int bypassed_init(int value) {
  int result = 0;
  switch (value) {
    int local;
  case 0:
    local = 7;
  case 1:
    result = local;
    break;
  default:
    result = local;
  }
  return result;
}

// INIT-LABEL: define{{.*}} i32 @bypassed_init(
// INIT: %local = alloca i32
// INIT: store i32 [[VALUE]], ptr %local
// INIT: br label %sw.dispatch
// INIT-NOT: store i32 [[VALUE]], ptr %local
// INIT: ret i32
