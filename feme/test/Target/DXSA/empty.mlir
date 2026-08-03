// RUN: feme-translate --import-dxsa-bin | FileCheck %s
// RUN: feme-translate --import-dxsa-bin | feme-opt --verify-roundtrip

// CHECK:      dxsa.module {
// CHECK-NEXT: }
