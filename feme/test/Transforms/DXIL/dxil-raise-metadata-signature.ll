; RUN: feme-opt --llvm -passes=feme-dxil-raise-metadata -S %s | FileCheck %s

; feme::dxil::MetadataRaisingPass preserves an entry point's
; input/output/patch-constant signature rows and root-signature bytes from
; `!dx.entryPoints` into `!feme.signature`/`!feme.dxil.rootsignature`
; function metadata before erasing the source metadata (roadmap R18; see
; "Signature reflection" in feme/docs/FeMeGraphicsDesign.md). The signature
; rows are converted to feme's source-independent `feme::EntrySignature`
; model (feme/include/feme/Core/Signature.h) and serialized with
; `feme::serializeSignature`; the root-signature bytes (its `EntryRootSigTag`
; (12) property) are kept verbatim, since FeMe does not parse a root
; signature's contents yet (roadmap W2).

target triple = "dxil-ms-dx"

; CHECK: target triple = "dxil-unknown-shadermodel6.0-vertex"

; CHECK: define void @main() [[ATTRS:#[0-9]+]] !feme.dxil.rootsignature ![[ROOTSIG:[0-9]+]] !feme.signature ![[SIG:[0-9]+]]
define void @main() {
  ret void
}

; CHECK-DAG: attributes [[ATTRS]] = {{{.*}}"feme.shader.stage"="vertex"{{.*}}"hlsl.shader"="vertex"{{.*}}}

; The serialized `feme::EntrySignature` bytes: not checked byte-for-byte
; here (see unittests/Transforms/DXIL/SignatureImportTest.cpp for that),
; just that a signature was attached at all.
; CHECK-DAG: ![[SIG]] = !{[{{[0-9]+}} x i8] c"{{.*}}"}
; The root-signature bytes are preserved verbatim.
; CHECK-DAG: ![[ROOTSIG]] = !{[4 x i8] c"\01\02\03\04"}

; `!dx.entryPoints` itself is still erased along with everything else
; `DXILTranslateMetadata` regenerates from scratch.
; CHECK-NOT: !dx.entryPoints

!dx.shaderModel = !{!0}
!dx.entryPoints = !{!1}

!0 = !{!"vs", i32 6, i32 0}
!1 = !{ptr @main, !"main", !2, null, !8}

; Signatures tuple: {Input, Output, PatchConstant}. One arbitrary input
; element (POSITION, float4, register 0) and one system-value output
; element (SV_Position); no patch-constant rows for a vertex shader.
!2 = !{!3, !5, null}
!3 = !{!4}
!4 = !{i32 0, !"POSITION", i8 9, i8 0, !6, i8 2, i32 1, i8 4, i32 0, i8 0, null}
!5 = !{!7}
!6 = !{i32 0}
!7 = !{i32 0, !"SV_Position", i8 9, i8 3, !6, i8 0, i32 1, i8 4, i32 0, i8 0, null}

; EntryRootSigTag (12): the entry's serialized root signature, as a raw
; byte blob.
!8 = !{i32 12, !9}
!9 = !{[4 x i8] c"\01\02\03\04"}
