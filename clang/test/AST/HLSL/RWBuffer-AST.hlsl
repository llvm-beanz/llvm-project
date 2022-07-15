// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-library -x hlsl -fsyntax-only -ast-dump -DEMPTY %s | FileCheck -check-prefix=EMPTY %s 
// RUN: %clang_cc1 -triple dxil-pc-shadermodel6.0-library -x hlsl -fsyntax-only -ast-dump %s | FileCheck %s 

// EMPTY: ClassTemplateDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> implicit RWBuffer
// EMPTY-NEXT: TemplateTypeParmDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> class depth 0 index 0 element_type
// EMPTY-NEXT: TemplateArgument type 'float'
// EMPTY-NEXT: BuiltinType 0x{{[0-9A-Fa-f]+}} 'float'
// EMPTY-NEXT: CXXRecordDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> implicit <undeserialized declarations> class RWBuffer
// EMPTY-NEXT: FinalAttr 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> Implicit final

// There should be no more occurrances of RWBuffer
// EMPTY-NOT: RWBuffer

#ifndef EMPTY

RWBuffer<float> Buffer;

#endif

// CHECK: CXXRecordDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> implicit <undeserialized declarations> class Resource definition
// CHECK: FinalAttr 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> Implicit final
// CHECK-NEXT: FieldDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> implicit h 'void *'

// CHECK: ClassTemplateDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> implicit RWBuffer
// CHECK-NEXT: TemplateTypeParmDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> class depth 0 index 0 element_type
// CHECK-NEXT: TemplateArgument type 'float'
// CHECK-NEXT: BuiltinType 0x{{[0-9A-Fa-f]+}} 'float'
// CHECK-NEXT: CXXRecordDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> implicit class RWBuffer definition

// CHECK: FinalAttr 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> Implicit final
// CHECK-NEXT: HLSLResourceAttr 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> Implicit UAV
// CHECK-NEXT: FieldDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> implicit h 'void *'
// CHECK: ClassTemplateSpecializationDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc> class RWBuffer definition

// CHECK: TemplateArgument type 'float'
// CHECK-NEXT: BuiltinType 0x{{[0-9A-Fa-f]+}} 'float'
// CHECK-NEXT: FinalAttr 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> Implicit final
// CHECK-NEXT: HLSLResourceAttr 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> Implicit UAV
// CHECK-NEXT: FieldDecl 0x{{[0-9A-Fa-f]+}} <<invalid sloc>> <invalid sloc>  implicit referenced h 'void *'
