// RUN: %clang_cc1 -triple spirv-unknown-vulkan-library -finclude-default-header -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s

struct Inputs {
  float smooth : SMOOTH;
  nointerpolation float flat : FLAT;
  noperspective float affine : AFFINE;
  centroid float centro : CENTRO;
  noperspective centroid float affineCentro : AFFINECENTRO;
  sample float sampled : SAMPLED;
  noperspective sample float affineSampled : AFFINESAMPLED;
  linear center float centered : CENTERED;
};

[shader("pixel")]
float main(Inputs input) : SV_Target {
  return input.smooth + input.flat + input.affine + input.centro +
         input.affineCentro + input.sampled + input.affineSampled +
         input.centered;
}

// CHECK-DAG: @SMOOTH0 = {{.*}}constant float, !spirv.Decorations ![[SMOOTH:[0-9]+]]
// CHECK-DAG: @FLAT0 = {{.*}}constant float, !spirv.Decorations ![[FLAT:[0-9]+]]
// CHECK-DAG: @AFFINE0 = {{.*}}constant float, !spirv.Decorations ![[AFFINE:[0-9]+]]
// CHECK-DAG: @CENTRO0 = {{.*}}constant float, !spirv.Decorations ![[CENTRO:[0-9]+]]
// CHECK-DAG: @AFFINECENTRO0 = {{.*}}constant float, !spirv.Decorations ![[AFFINECENTRO:[0-9]+]]
// CHECK-DAG: @SAMPLED0 = {{.*}}constant float, !spirv.Decorations ![[SAMPLED:[0-9]+]]
// CHECK-DAG: @AFFINESAMPLED0 = {{.*}}constant float, !spirv.Decorations ![[AFFINESAMPLED:[0-9]+]]
// CHECK-DAG: @CENTERED0 = {{.*}}constant float, !spirv.Decorations ![[CENTERED:[0-9]+]]

struct Inherited {
  float inherited : INHERITED;
  linear float overridden : OVERRIDDEN;
};

[shader("vertex")]
centroid Inherited vertex(float value : VALUE) {
  Inherited result = {value, value};
  return result;
}

[shader("pixel")]
float pixel(noperspective sample Inherited input) : SV_Target {
  return input.inherited + input.overridden;
}

// The enclosing declaration's modifiers reach each leaf, but an explicit
// modifier on a field replaces the inherited mask. Vertex outputs use the
// same decorations as pixel inputs; vertex inputs remain uninterpolated.
// CHECK-DAG: @VALUE0 = {{.*}}constant float, !spirv.Decorations ![[VALUE:[0-9]+]]
// CHECK-DAG: @INHERITED0 = {{.*}}global float, !spirv.Decorations ![[VOUT:[0-9]+]]
// CHECK-DAG: @OVERRIDDEN0 = {{.*}}global float, !spirv.Decorations ![[VLINEAR:[0-9]+]]
// CHECK-DAG: @INHERITED0.1 = {{.*}}constant float, !spirv.Decorations ![[PIN:[0-9]+]]
// CHECK-DAG: @OVERRIDDEN0.2 = {{.*}}constant float, !spirv.Decorations ![[PLINEAR:[0-9]+]]

// CHECK-DAG: ![[SMOOTH]] = !{![[SMOOTHLOC:[0-9]+]]}
// CHECK-DAG: ![[FLAT]] = !{!{{[0-9]+}}, ![[FLATDEC:[0-9]+]]}
// CHECK-DAG: ![[AFFINE]] = !{!{{[0-9]+}}, ![[NOPERS:[0-9]+]]}
// CHECK-DAG: ![[CENTRO]] = !{!{{[0-9]+}}, ![[CENTROID:[0-9]+]]}
// CHECK-DAG: ![[AFFINECENTRO]] = !{!{{[0-9]+}}, ![[NOPERS]], ![[CENTROID]]}
// CHECK-DAG: ![[SAMPLED]] = !{!{{[0-9]+}}, ![[SAMPLE:[0-9]+]]}
// CHECK-DAG: ![[AFFINESAMPLED]] = !{!{{[0-9]+}}, ![[NOPERS]], ![[SAMPLE]]}
// CHECK-DAG: ![[CENTERED]] = !{!{{[0-9]+}}}
// CHECK-DAG: ![[VALUE]] = !{!{{[0-9]+}}}
// CHECK-DAG: ![[VOUT]] = !{!{{[0-9]+}}, ![[CENTROID]]}
// CHECK-DAG: ![[VLINEAR]] = !{!{{[0-9]+}}}
// CHECK-DAG: ![[PIN]] = !{!{{[0-9]+}}, ![[NOPERS]], ![[SAMPLE]]}
// CHECK-DAG: ![[PLINEAR]] = !{!{{[0-9]+}}}
// CHECK-DAG: ![[SMOOTHLOC]] = !{i32 30, i32 0}
// CHECK-DAG: ![[FLATDEC]] = !{i32 14}
// CHECK-DAG: ![[NOPERS]] = !{i32 13}
// CHECK-DAG: ![[CENTROID]] = !{i32 16}
// CHECK-DAG: ![[SAMPLE]] = !{i32 17}
