// RUN: %clang_cc1 -triple spirv-unknown-vulkan-library -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s --implicit-check-not='switch i'
// RUN: %clang_cc1 -triple dxil-unknown-shadermodel6.3-library -fno-switch \
// RUN:   -emit-llvm -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --implicit-check-not='switch i'
// RUN: %clang_cc1 -x c -Dexport= -fno-switch -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s --implicit-check-not='switch i'
// RUN: %clang --target=spirv-unknown-vulkan-library -O0 -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=SPIRV --implicit-check-not=OpSwitch
// RUN: %clang --target=spirv-unknown-vulkan-library -O2 -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=OPT --implicit-check-not=OpSwitch
// RUN: %if spirv-tools %{ %clang --target=spirv-unknown-vulkan-library \
// RUN:   -O0 -c -o %t.spv %s && spirv-val %t.spv %}
// RUN: %if spirv-tools %{ %clang --target=spirv-unknown-vulkan-library \
// RUN:   -O2 -c -o %t.opt.spv %s && spirv-val %t.opt.spv %}

// Exercise both nesting directions for each control-flow construct. Each switch
// has adjacent labels (trivial fallthrough) and a fallthrough after executable
// code. Check the fallthrough PHIs and exit targets, not just branch counts.

export int switch_in_for(int value, int condition) {
  int result = 0;
  for (int index = 0; index < 3; ++index) {
    switch (value) {
    case 0:
    case 1:
      result += 1;
      if (condition)
        continue;
    case 2:
      result += 2;
      break;
    default:
      result += 4;
    }
    result += 8;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 {{.*}}switch_in_for
// CHECK: for.body:
// CHECK: br label %sw.dispatch
// CHECK: sw.bb:
// CHECK-NEXT: br label %[[NEXT:sw.next[0-9]*]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: if.then:
// CHECK-NEXT: br label %for.inc
// CHECK: if.end:
// CHECK-NEXT: br label %[[NEXT2:sw.next[0-9]+]]
// CHECK: [[NEXT2]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %if.end ], [ false, %[[NEXT]] ]
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %sw.epilog
// CHECK: sw.epilog:
// CHECK: add nsw i32 %{{.*}}, 8
// CHECK: br label %for.inc
// CHECK: for.inc:
// CHECK: br label %for.cond

export int for_in_switch(int value, int count) {
  int result = 0;
  switch (value) {
  case 0:
  case 1:
    for (int index = 0; index < count; ++index) {
      if (index == 2)
        break;
      result += 1;
    }
  case 2:
    result += 2;
  default:
    result += 4;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 {{.*}}for_in_switch
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: if.then:
// CHECK-NEXT: br label %for.end
// CHECK: for.end:
// CHECK-NEXT: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %for.end ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %[[DEFAULT:sw.next[0-9]+]]
// CHECK: [[DEFAULT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %{{sw.bb[0-9]+}} ], [ false, %[[NEXT]] ]
// CHECK: add nsw i32 %{{.*}}, 4

export int switch_in_while(int value, int condition) {
  int result = 0;
  int index = 0;
  while (index++ < 3) {
    switch (value) {
    case 0:
    case 1:
      result += 1;
      if (condition)
        continue;
    case 2:
      result += 2;
      break;
    default:
      result += 4;
    }
    result += 8;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 {{.*}}switch_in_while
// CHECK: while.body:
// CHECK: br label %sw.dispatch
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: if.then:
// CHECK-NEXT: br label %while.cond
// CHECK: if.end:
// CHECK-NEXT: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %if.end ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %sw.epilog
// CHECK: sw.epilog:
// CHECK: add nsw i32 %{{.*}}, 8
// CHECK: br label %while.cond

export int while_in_switch(int value, int count) {
  int result = 0;
  switch (value) {
  case 0:
  case 1:
    while (count-- > 0) {
      if (count == 2)
        continue;
      if (count == 1)
        break;
      result += 1;
    }
  case 2:
    result += 2;
  default:
    result += 4;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 {{.*}}while_in_switch
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: if.then:
// CHECK-NEXT: br label %while.cond
// CHECK: if.then{{[0-9]+}}:
// CHECK-NEXT: br label %while.end
// CHECK: while.end:
// CHECK-NEXT: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %while.end ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %[[DEFAULT:sw.next[0-9]+]]
// CHECK: [[DEFAULT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %{{sw.bb[0-9]+}} ], [ false, %[[NEXT]] ]
// CHECK: add nsw i32 %{{.*}}, 4

export int switch_in_if(int value, int condition) {
  int result = 0;
  if (condition) {
    switch (value) {
    case 0:
    case 1:
      result += 1;
    case 2:
      result += 2;
      break;
    default:
      result += 4;
    }
  } else {
    result = 8;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 {{.*}}switch_in_if
// CHECK: if.then:
// CHECK: br label %sw.dispatch
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: add nsw i32 %{{.*}}, 1
// CHECK: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %{{sw.bb[0-9]+}} ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %sw.epilog
// CHECK: sw.epilog:
// CHECK-NEXT: br label %if.end
// CHECK: if.else:
// CHECK: store i32 8
// CHECK: br label %if.end

export int if_in_switch(int value, int condition) {
  int result = 0;
  switch (value) {
  case 0:
  case 1:
    if (condition)
      result += 1;
    else
      result += 2;
  case 2:
    result += 4;
    break;
  default:
    result += 8;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 {{.*}}if_in_switch
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: if.then:
// CHECK: add nsw i32 %{{.*}}, 1
// CHECK: br label %if.end
// CHECK: if.else:
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %if.end
// CHECK: if.end:
// CHECK-NEXT: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %if.end ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 4
// CHECK: br label %sw.epilog

export int switch_in_switch(int outer, int inner) {
  int result = 0;
  switch (outer) {
  case 0:
  case 1:
    switch (inner) {
    case 0:
    case 1:
      result += 1;
    case 2:
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

// CHECK-LABEL: define{{.*}} i32 {{.*}}switch_in_switch
// CHECK: %[[OUTER:[^ ]+]] = load i32, ptr %outer.addr
// CHECK: sw.dispatch:
// CHECK: icmp eq i32 %[[OUTER]], 0
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: %[[INNER:[^ ]+]] = load i32, ptr %inner.addr
// CHECK: br label %[[DISPATCH:sw.dispatch[0-9]+]]
// CHECK: [[DISPATCH]]:
// CHECK: icmp eq i32 %[[INNER]], 0
// CHECK: [[INNER_NEXT:sw.next[0-9]+]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %{{sw.bb[0-9]+}} ], [ false, %[[DISPATCH]] ]
// CHECK: add nsw i32 %{{.*}}, 1
// CHECK: [[INNER_NEXT2:sw.next[0-9]+]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %{{sw.bb[0-9]+}} ], [ false, %[[INNER_NEXT]] ]
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %[[INNER_EXIT:sw.epilog[0-9]*]]
// CHECK: [[INNER_EXIT]]:
// CHECK-NEXT: br label %[[OUTER_NEXT:sw.next[0-9]+]]
// CHECK: [[OUTER_NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %[[INNER_EXIT]] ], [ false, %sw.next ]
// CHECK: icmp eq i32 %[[OUTER]], 2
// CHECK: add nsw i32 %{{.*}}, 8
// CHECK: br label %{{sw.epilog[0-9]+}}

export int constant_switch_in_switch(int value, int unused) {
  int result = 0;
  switch (value) {
  case 0:
  case 1:
    switch (1) {
    case 0:
      result += 64;
      break;
    case 1:
      result += 1;
    case 2:
      result += 2;
    }
  case 2:
    result += 4;
  default:
    result += 8;
  }
  return result;
}

// CHECK-LABEL: define{{.*}} i32 {{.*}}constant_switch_in_switch
// CHECK: sw.next:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %sw.bb ], [ false, %sw.dispatch ]
// CHECK: add nsw i32 %{{.*}}, 1
// CHECK: add nsw i32 %{{.*}}, 2
// CHECK: br label %[[NEXT:sw.next[0-9]+]]
// CHECK: [[NEXT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %{{sw.bb[0-9]+}} ], [ false, %sw.next ]
// CHECK: add nsw i32 %{{.*}}, 4
// CHECK: br label %[[DEFAULT:sw.next[0-9]+]]
// CHECK: [[DEFAULT]]:
// CHECK-NEXT: %{{.*}} = phi i1 [ true, %{{sw.bb[0-9]+}} ], [ false, %[[NEXT]] ]
// CHECK: add nsw i32 %{{.*}}, 8

// SPIRV: OpLoopMerge
// SPIRV: OpSelectionMerge
// SPIRV: OpBranchConditional
// OPT: OpFunctionEnd
