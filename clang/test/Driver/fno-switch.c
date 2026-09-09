// RUN: %clang -### -fno-switch %s 2>&1 | FileCheck %s --check-prefix=NO-SWITCH
// RUN: %clang -### -fno-switch -fswitch %s 2>&1 | FileCheck %s --check-prefix=SWITCH
// RUN: %clang -### -fswitch -fno-switch %s 2>&1 | FileCheck %s --check-prefix=NO-SWITCH

// NO-SWITCH: "-fno-switch"
// SWITCH-NOT: "-fno-switch"
