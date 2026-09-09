// RUN: %clang -### -fno-switch %s 2>&1 | FileCheck %s --check-prefix=NO-SWITCH
// RUN: %clang -### -fno-switch -fswitch %s 2>&1 | FileCheck %s --check-prefix=SWITCH
// RUN: %clang -### -fswitch -fno-switch %s 2>&1 | FileCheck %s --check-prefix=NO-SWITCH
// RUN: %clang -### -target spirv-unknown-vulkan-library -x hlsl -fswitch %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=SWITCH-EXPLICIT

// NO-SWITCH: "-fno-switch"
// SWITCH-NOT: "-fno-switch"
// SWITCH-EXPLICIT: "-fswitch"
