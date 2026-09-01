// RUN: feme-opt --feme-convert-spirv-to-llvm %s | FileCheck %s

// Roadmap L17: a fixed-size array of a *scalar* type (`uint x[2]`) inside a
// struct/cbuffer, immediately followed by another sibling member (`uint
// q`) -- a real `-fvk-use-dx-layout` shape (`Feature/CBuffer/array-of-
// structs.test`/`dynamic-struct.test`'s own `struct S { uint x[2]; uint
// q; };`) whose declared layout is `struct<S, (array<2 x i32, stride=16>
// [0], i32 [20])>`: real HLSL/Vulkan layout rules require every array
// element except the last to occupy the full declared `ArrayStride` (so a
// dynamic index into `x[1]` still computes the right byte offset), but the
// array's own footprint for a *subsequent* sibling member's own placement
// is only `(N-1)*Stride + NaturalSize(last element)` -- i.e. `q` is
// allowed to pack into the otherwise-unused trailing padding of `x`'s own
// last element, at offset 20, not 32 (a uniformly Stride-wide array's own
// full footprint).
//
// Fixed by `convertUndersizedScalarArrayMemberIgnoringDecorations`
// (`SPIRVToLLVMPatterns.cpp`), retried from
// `convertOffsetStructTypeIgnoringDecorations` whenever the array
// member's own uniformly-widened representation (a plain
// `LLVM::LLVMArrayType` of `Stride`-sized byte-array stand-ins, built by
// `convertArrayTypeIgnoringDecorations`'s own generalized scalar-array
// case) does not reproduce `q`'s declared offset: it instead represents
// `x` as one literal struct member per array element -- every element
// except the last using a `Stride`-sized opaque byte-array stand-in (same
// GEP-addressing depth as the element itself, safe since this codebase's
// pointers are all opaque -- see `getTightVectorArrayType`'s identical
// reasoning), the last element keeping its own real, unpadded type, so
// `q`'s own placement immediately afterward reproduces its declared
// offset exactly. Every access below uses only compile-time-constant
// SPIR-V indices into `x` (as real `dxc`-emitted code always does for a
// literal HLSL array index), which is all `LLVM::GEPOp` needs to
// navigate a literal struct member this way.
//
// Note this project's own resource-access pattern splits a
// `spirv.AccessChain` into an `llvm.spv.resource.getpointer` call (whose
// own index consumes the *first* struct-member selector, i.e. which of
// `S`'s members) plus a residual `llvm.getelementptr` for any further
// indices, so `x`'s own residual GEP below is computed against `x`'s type
// in isolation -- the plain, uniformly-widened array
// (`convertArrayTypeIgnoringDecorations`'s generic case) rather than this
// file's own heterogeneous struct-of-elements shape. Both give identical
// byte offsets for every element actually addressed here (0 and Stride),
// since only the array's *last* element's size differs between the two
// representations, and no access below indexes past a whole element.

// The resource's own declared type reproduces the heterogeneous
// struct-of-elements shape for `x` (last element unpadded), with `q`
// immediately following as S's own second member:
// CHECK-LABEL: llvm.func @scalar_array_with_trailing_sibling
// CHECK: llvm.call_intrinsic "llvm.spv.resource.handlefrombinding"
// CHECK-SAME: !llvm.target<"spirv.VulkanBuffer", !llvm.struct<(struct<packed (array<16 x i8>, i32)>, i32)>

// x[0]: `getpointer`'s own index selects S's first member (`x`, index 0);
// the residual GEP navigates `x`'s own type in isolation -- a plain,
// uniformly Stride-wide array of byte-array stand-ins (computed the same
// way a non-sibling-constrained array would be) -- which still yields the
// right byte offset (0) for element 0, independent of the GEP's own
// nominal pointee type (opaque pointers throughout; the load below
// specifies i32 explicitly):
// CHECK: %[[PTR0:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: %[[GEP0:.*]] = llvm.getelementptr inbounds %[[PTR0]][0, %{{.*}}] : {{.*}}, !llvm.array<2 x array<16 x i8>>
// CHECK: %[[X0:.*]] = llvm.load %[[GEP0]]

// x[1]: the array's own last element, offset 16 within `x` -- still
// correct despite the residual GEP's own type not reflecting the last
// element's real (unpadded, and thus not independently addressable
// beyond its own start) type, since only a whole-element load is ever
// performed here:
// CHECK: %[[PTR1:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: %[[GEP1:.*]] = llvm.getelementptr inbounds %[[PTR1]][0, %{{.*}}] : {{.*}}, !llvm.array<2 x array<16 x i8>>
// CHECK: %[[X1:.*]] = llvm.load %[[GEP1]]

// q: S's own second member (index 1), fully resolved by `getpointer`'s
// own index with no residual GEP needed -- reproducing its declared
// offset (20, not 32) exactly:
// CHECK: %[[Q:.*]] = llvm.call_intrinsic "llvm.spv.resource.getpointer"
// CHECK: llvm.load %[[Q]]

// CHECK: %[[SUM0:.*]] = llvm.add %[[X0]], %[[X1]]
// CHECK: %[[SUM1:.*]] = llvm.add %[[SUM0]], %{{.*}}
// CHECK: llvm.return %[[SUM1]]

spirv.module Logical GLSL450 requires #spirv.vce<v1.6, [Shader], []> {
  spirv.GlobalVariable @CB bind(0, 0) : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<S, (!spirv.array<2 x si32, stride=16> [0], si32 [20])> [0]), Block>, Uniform>
  spirv.func @scalar_array_with_trailing_sibling() -> si32 "None" {
    %addr = spirv.mlir.addressof @CB : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<S, (!spirv.array<2 x si32, stride=16> [0], si32 [20])> [0]), Block>, Uniform>
    %c0 = spirv.Constant 0 : si32
    %c1 = spirv.Constant 1 : si32
    %ac0 = spirv.AccessChain %addr[%c0, %c0, %c0] : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<S, (!spirv.array<2 x si32, stride=16> [0], si32 [20])> [0]), Block>, Uniform>, si32, si32, si32 -> !spirv.ptr<si32, Uniform>
    %x0 = spirv.Load "Uniform" %ac0 : si32
    %ac1 = spirv.AccessChain %addr[%c0, %c0, %c1] : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<S, (!spirv.array<2 x si32, stride=16> [0], si32 [20])> [0]), Block>, Uniform>, si32, si32, si32 -> !spirv.ptr<si32, Uniform>
    %x1 = spirv.Load "Uniform" %ac1 : si32
    %ac2 = spirv.AccessChain %addr[%c0, %c1] : !spirv.ptr<!spirv.struct<type.CB, (!spirv.struct<S, (!spirv.array<2 x si32, stride=16> [0], si32 [20])> [0]), Block>, Uniform>, si32, si32 -> !spirv.ptr<si32, Uniform>
    %q = spirv.Load "Uniform" %ac2 : si32
    %sum0 = spirv.IAdd %x0, %x1 : si32
    %sum1 = spirv.IAdd %sum0, %q : si32
    spirv.ReturnValue %sum1 : si32
  }
}
