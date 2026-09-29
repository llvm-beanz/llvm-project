; RUN: feme-opt --llvm -passes=feme-cpu-simdize -feme-cpu-wave-size=4 -S %s | FileCheck %s

; Roadmap L266: `dEQP-VK.glsl.builtin.function.integer`'s `findMSB`/
; `findlsb`/`uaddcarry`/`usubborrow`/`{i,u}mulextended` builtins lower to
; `llvm.ctlz`/`llvm.cttz`/`llvm.{u,s}{add,sub,mul}.with.overflow`
; respectively, none of which the pre-L266 `isElementwiseVectorizableIntrinsic`
; gate could widen when the call is divergent in a non-`compute` shader
; stage: `llvm.ctlz`/`cttz` fail the plain `Homogeneous` (same-type-
; everywhere) check purely because of their second, `ImmArg`-attributed
; `is_zero_poison` operand (a compile-time constant, not a genuinely
; per-lane one); the four with-overflow intrinsics have a genuinely
; aggregate (`{iN, i1}`) result type, which neither of `widenElementwise`'s
; two flat-`<W x T>`-result paths can build at all. Both classes now widen:
; `ctlz`/`cttz` via `getDivergentCallOverloadShape` (like `is_fpclass`'s own
; almost-Homogeneous shape), and the with-overflow intrinsics via their own
; dedicated `widenOverflowArithIntrinsic`, which builds a single real
; vector-typed overload of the same call (no per-lane cloning needed, since
; these are pure computations with no memory side effect to sequence) and
; splits its own two `<W x iN>`/`<W x i1>` fields out via `extractvalue`.

; CHECK-LABEL: define void @main(
; The `is_zero_poison` immarg operand of both `ctlz`/`cttz` stays a scalar
; `i1`, unwidened, even though the vector overload's other operand is the
; widened `<4 x i32>`.
; CHECK: [[CTLZ:%.*]] = call <4 x i32> @llvm.ctlz.v4i32(<4 x i32> {{%.*}}, i1 false)
; CHECK: [[CTTZ:%.*]] = call <4 x i32> @llvm.cttz.v4i32(<4 x i32> {{%.*}}, i1 false)
; A single vector-typed `with.overflow` call computes every lane's
; `{<4 x i32>, <4 x i1>}` pair at once, split apart via `extractvalue`
; rather than a per-lane clone loop.
; CHECK: [[ADDC:%.*]] = call { <4 x i32>, <4 x i1> } @llvm.uadd.with.overflow.v4i32(<4 x i32> {{%.*}}, <4 x i32> {{.*}})
; CHECK: extractvalue { <4 x i32>, <4 x i1> } [[ADDC]], 0
; CHECK: extractvalue { <4 x i32>, <4 x i1> } [[ADDC]], 1
; CHECK: [[MULE:%.*]] = call { <4 x i32>, <4 x i1> } @llvm.umul.with.overflow.v4i32(<4 x i32> {{%.*}}, <4 x i32> {{.*}})
; CHECK: extractvalue { <4 x i32>, <4 x i1> } [[MULE]], 0
; CHECK: extractvalue { <4 x i32>, <4 x i1> } [[MULE]], 1
; CHECK-COUNT-4: call void @feme.cpu.resource.store.raw.i32(
define void @main(ptr %resource_heap, i32 %resource_heap_count) #0 {
  %tid = call i32 @llvm.dx.thread.id(i32 0)
  %off = zext i32 %tid to i64

  %ctlz = call i32 @llvm.ctlz.i32(i32 %tid, i1 false)
  %cttz = call i32 @llvm.cttz.i32(i32 %tid, i1 false)

  %addc = call { i32, i1 } @llvm.uadd.with.overflow.i32(i32 %tid, i32 1)
  %addc.val = extractvalue { i32, i1 } %addc, 0
  %addc.ovf = extractvalue { i32, i1 } %addc, 1
  %addc.ovf32 = zext i1 %addc.ovf to i32

  %mule = call { i32, i1 } @llvm.umul.with.overflow.i32(i32 %tid, i32 2)
  %mule.val = extractvalue { i32, i1 } %mule, 0
  %mule.ovf = extractvalue { i32, i1 } %mule, 1
  %mule.ovf32 = zext i1 %mule.ovf to i32

  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i32 %ctlz, i1 true)
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i32 %cttz, i1 true)
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i32 %addc.val, i1 true)
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i32 %addc.ovf32, i1 true)
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i32 %mule.val, i1 true)
  call void @feme.cpu.resource.store.raw.i32(ptr %resource_heap, i32 %resource_heap_count, i32 0, i64 %off, i32 %mule.ovf32, i1 true)
  ret void
}
declare void @feme.cpu.resource.store.raw.i32(ptr, i32, i32, i64, i32, i1)
declare i32 @llvm.dx.thread.id(i32)
declare i32 @llvm.ctlz.i32(i32, i1)
declare i32 @llvm.cttz.i32(i32, i1)
declare { i32, i1 } @llvm.uadd.with.overflow.i32(i32, i32)
declare { i32, i1 } @llvm.umul.with.overflow.i32(i32, i32)
attributes #0 = { "hlsl.shader"="compute" "hlsl.numthreads"="4,1,1" }
