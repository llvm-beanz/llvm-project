// RUN: %clang_cc1 -triple spirv-unknown-vulkan-library -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=BRANCH
// RUN: %clang_cc1 -triple spirv-unknown-vulkan-library -fswitch -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=SWITCH
// RUN: %clang_cc1 -triple dxil-unknown-shadermodel6.3-library -emit-llvm \
// RUN:   -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=SWITCH
// RUN: %clang --target=spirv-unknown-vulkan-library -x hlsl -O0 -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=SPIRV
// RUN: %if spirv-tools %{ %clang --target=spirv-unknown-vulkan-library -x hlsl \
// RUN:   -O0 -c -o %t.spv %s && spirv-val %t.spv %}

export int select_value(int value) {
  switch (value) {
  case 0:
    return 10;
  default:
    return 20;
  }
}

// BRANCH-LABEL: define{{.*}} i32 {{.*}}select_value
// BRANCH-NOT: switch
// BRANCH: br i1

// SWITCH-LABEL: define{{.*}} i32 {{.*}}select_value
// SWITCH: switch i32

export int select_with_hint(int value) {
  [branch]
  switch (value) {
  case 0:
    return 10;
  default:
    return 20;
  }
}

// BRANCH-LABEL: define{{.*}} i32 {{.*}}select_with_hint
// BRANCH-COUNT-2: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !hlsl.controlflow.hint [[BRANCH_HINT:![0-9]+]]

export int switch_in_for_loop(int value) {
  int result = 0;
  for (int index = 0; index < 4; ++index) {
    switch ((value + index) & 3) {
    case 0:
      result += 1;
    case 1:
      result += 10;
      break;
    case 2:
      result += 100;
    default:
      result += 1000;
      break;
    }
  }
  return result;
}

export int switch_in_while_loop(int value) {
  int result = 0;
  int index = 0;
  while (index < 4) {
    switch ((value + index) & 3) {
    case 0:
      result += 1;
    case 1:
      result += 10;
      break;
    case 2:
      result += 100;
    default:
      result += 1000;
      break;
    }
    ++index;
  }
  return result;
}

// BRANCH-LABEL: define{{.*}} i32 {{.*}}switch_in_for_loop
// BRANCH-NOT: switch
// BRANCH: br i1

// BRANCH-LABEL: define{{.*}} i32 {{.*}}switch_in_while_loop
// BRANCH-NOT: switch
// BRANCH: br i1

// BRANCH: [[BRANCH_HINT]] = !{!"hlsl.controlflow.hint", i32 1}

// SPIRV-NOT: OpSwitch
// SPIRV-COUNT-2: OpLoopMerge
// SPIRV: OpSelectionMerge
// SPIRV: OpBranchConditional
