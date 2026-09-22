; ModuleID = '<in-memory object>'
source_filename = "LLVMDialectModule"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32"

@spirv_var_110.str = private constant [14 x i8] c"spirv_var_110\00"
@spirv_var_101 = external addrspace(8) global <{ <4 x float>, float, [1 x float], [1 x float], [4 x i8] }>, !feme.spirv.MemberDecorations !12
@spirv_var_103 = external addrspace(7) constant <4 x float>, !spirv.Decorations !14
@spirv_var_122 = external addrspace(8) global float, !spirv.Decorations !14

define void @compute() {
  %1 = alloca [3 x <3 x float>], align 4
  %2 = alloca [3 x <3 x float>], align 4
  %3 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 0, i32 0)
  %4 = insertelement <4 x float> poison, float %3, i64 0
  %5 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 1, i32 0)
  %6 = insertelement <4 x float> %4, float %5, i64 1
  %7 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 2, i32 0)
  %8 = insertelement <4 x float> %6, float %7, i64 2
  %9 = call float @feme.stage.input.load.f32(i32 0, i32 0, i32 3, i32 0)
  %10 = insertelement <4 x float> %8, float %9, i64 3
  %11 = extractelement <4 x float> %10, i64 0
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 0, float %11, i32 0)
  %12 = extractelement <4 x float> %10, i64 1
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 1, float %12, i32 0)
  %13 = extractelement <4 x float> %10, i64 2
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 2, float %13, i32 0)
  %14 = extractelement <4 x float> %10, i64 3
  call void @feme.stage.output.store.f32(i32 1, i32 0, i32 3, float %14, i32 0)
  %16 = addrspacecast ptr @ubo_buffer to ptr addrspace(12)
  %.unpack.unpack.unpack = load float, ptr addrspace(12) %16, align 1
  %.unpack.unpack.elt12 = getelementptr inbounds nuw i8, ptr addrspace(12) %16, i64 4
  %.unpack.unpack.unpack13 = load float, ptr addrspace(12) %.unpack.unpack.elt12, align 1
  %.unpack.unpack.elt14 = getelementptr inbounds nuw i8, ptr addrspace(12) %16, i64 8
  %.unpack.unpack.unpack15 = load float, ptr addrspace(12) %.unpack.unpack.elt14, align 1
  %.elt5 = getelementptr inbounds nuw i8, ptr addrspace(12) %16, i64 16
  %.unpack6.unpack.unpack = load float, ptr addrspace(12) %.elt5, align 1
  %.unpack6.unpack.elt27 = getelementptr inbounds nuw i8, ptr addrspace(12) %16, i64 20
  %.unpack6.unpack.unpack28 = load float, ptr addrspace(12) %.unpack6.unpack.elt27, align 1
  %.unpack6.unpack.elt29 = getelementptr inbounds nuw i8, ptr addrspace(12) %16, i64 24
  %.unpack6.unpack.unpack30 = load float, ptr addrspace(12) %.unpack6.unpack.elt29, align 1
  %.elt7 = getelementptr inbounds nuw i8, ptr addrspace(12) %16, i64 32
  %.unpack8.unpack.unpack = load float, ptr addrspace(12) %.elt7, align 1
  %.unpack8.unpack.elt42 = getelementptr inbounds nuw i8, ptr addrspace(12) %16, i64 36
  %.unpack8.unpack.unpack43 = load float, ptr addrspace(12) %.unpack8.unpack.elt42, align 1
  %.unpack8.unpack.elt44 = getelementptr inbounds nuw i8, ptr addrspace(12) %16, i64 40
  %.unpack8.unpack.unpack45 = load float, ptr addrspace(12) %.unpack8.unpack.elt44, align 1
  %17 = insertelement <3 x float> poison, float %.unpack.unpack.unpack, i64 0
  %18 = insertelement <3 x float> %17, float %.unpack.unpack.unpack13, i64 1
  %19 = insertelement <3 x float> %18, float %.unpack.unpack.unpack15, i64 2
  %20 = insertelement <3 x float> poison, float %.unpack6.unpack.unpack, i64 0
  %21 = insertelement <3 x float> %20, float %.unpack6.unpack.unpack28, i64 1
  %22 = insertelement <3 x float> %21, float %.unpack6.unpack.unpack30, i64 2
  %23 = insertelement <3 x float> poison, float %.unpack8.unpack.unpack, i64 0
  %24 = insertelement <3 x float> %23, float %.unpack8.unpack.unpack43, i64 1
  %25 = insertelement <3 x float> %24, float %.unpack8.unpack.unpack45, i64 2
  store <3 x float> %19, ptr %1, align 4
  %.fca.1.gep2 = getelementptr inbounds nuw i8, ptr %1, i64 12
  store <3 x float> %22, ptr %.fca.1.gep2, align 4
  %.fca.2.gep3 = getelementptr inbounds nuw i8, ptr %1, i64 24
  store <3 x float> %25, ptr %.fca.2.gep3, align 4
  store <3 x float> zeroinitializer, ptr %2, align 4
  %.fca.1.gep = getelementptr inbounds nuw i8, ptr %2, i64 12
  store <3 x float> zeroinitializer, ptr %.fca.1.gep, align 4
  %.fca.2.gep = getelementptr inbounds nuw i8, ptr %2, i64 24
  store <3 x float> zeroinitializer, ptr %.fca.2.gep, align 4
  %26 = call float @spirv_fn_25(ptr nonnull %1, ptr nonnull %2)
  %v26d = fpext float %26 to double
  call i32 (ptr, ...) @printf(ptr @fmt, double %v26d)
  ret void
}

define internal float @spirv_fn_11(ptr %0, ptr %1) {
  %3 = load float, ptr %0, align 4
  %4 = load float, ptr %1, align 4
  %5 = fsub float %3, %4
  %6 = call float @llvm.fabs.f32(float %5)
  %7 = fcmp olt float %6, 5.000000e-02
  %8 = select i1 %7, float 1.000000e+00, float 0.000000e+00
  ret float %8
}

define internal float @spirv_fn_18(ptr %0, ptr %1) {
  %3 = alloca float, align 4
  %4 = alloca float, align 4
  %5 = alloca float, align 4
  %6 = alloca float, align 4
  %7 = alloca float, align 4
  %8 = alloca float, align 4
  %9 = getelementptr <3 x float>, ptr %0, i32 0, i32 0
  %10 = load float, ptr %9, align 4
  store float %10, ptr %3, align 4
  %11 = getelementptr <3 x float>, ptr %1, i32 0, i32 0
  %12 = load float, ptr %11, align 4
  store float %12, ptr %4, align 4
  %13 = call float @spirv_fn_11(ptr %3, ptr %4)
  %14 = getelementptr <3 x float>, ptr %0, i32 0, i32 1
  %15 = load float, ptr %14, align 4
  store float %15, ptr %5, align 4
  %16 = getelementptr <3 x float>, ptr %1, i32 0, i32 1
  %17 = load float, ptr %16, align 4
  store float %17, ptr %6, align 4
  %18 = call float @spirv_fn_11(ptr %5, ptr %6)
  %19 = fmul float %13, %18
  %20 = getelementptr <3 x float>, ptr %0, i32 0, i32 2
  %21 = load float, ptr %20, align 4
  store float %21, ptr %7, align 4
  %22 = getelementptr <3 x float>, ptr %1, i32 0, i32 2
  %23 = load float, ptr %22, align 4
  store float %23, ptr %8, align 4
  %24 = call float @spirv_fn_11(ptr %7, ptr %8)
  %25 = fmul float %19, %24
  ret float %25
}

define internal float @spirv_fn_25(ptr %0, ptr %1) {
  %3 = alloca <3 x float>, align 4
  %4 = alloca <3 x float>, align 4
  %5 = alloca <3 x float>, align 4
  %6 = alloca <3 x float>, align 4
  %7 = alloca <3 x float>, align 4
  %8 = alloca <3 x float>, align 4
  %9 = getelementptr [3 x <3 x float>], ptr %0, i32 0, i32 0
  %10 = load <3 x float>, ptr %9, align 4
  store <3 x float> %10, ptr %3, align 4
  %11 = getelementptr [3 x <3 x float>], ptr %1, i32 0, i32 0
  %12 = load <3 x float>, ptr %11, align 4
  store <3 x float> %12, ptr %4, align 4
  %13 = call float @spirv_fn_18(ptr %3, ptr %4)
  %14 = getelementptr [3 x <3 x float>], ptr %0, i32 0, i32 1
  %15 = load <3 x float>, ptr %14, align 4
  store <3 x float> %15, ptr %5, align 4
  %16 = getelementptr [3 x <3 x float>], ptr %1, i32 0, i32 1
  %17 = load <3 x float>, ptr %16, align 4
  store <3 x float> %17, ptr %6, align 4
  %18 = call float @spirv_fn_18(ptr %5, ptr %6)
  %19 = fmul float %13, %18
  %20 = getelementptr [3 x <3 x float>], ptr %0, i32 0, i32 2
  %21 = load <3 x float>, ptr %20, align 4
  store <3 x float> %21, ptr %7, align 4
  %22 = getelementptr [3 x <3 x float>], ptr %1, i32 0, i32 2
  %23 = load <3 x float>, ptr %22, align 4
  store <3 x float> %23, ptr %8, align 4
  %24 = call float @spirv_fn_18(ptr %7, ptr %8)
  %25 = fmul float %19, %24
  ret float %25
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)

; Function Attrs: convergent nocallback nofree nosync nounwind willreturn memory(none)

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fabs.f32(float) #3

define float @feme.stage.input.load.f32(i32, i32, i32, i32) { ret float 0.0 }

define void @feme.stage.output.store.f32(i32, i32, i32, float, i32) { ret void }

attributes #0 = { "feme.cpu.wavesize"="4" "feme.shader.stage"="vertex" "hlsl.shader"="vertex" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(none) }
attributes #2 = { convergent nocallback nofree nosync nounwind willreturn memory(none) }
attributes #3 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }

!0 = !{i32 11, i32 0}
!1 = !{!0}
!2 = !{i32 0, !1}
!3 = !{i32 11, i32 1}
!4 = !{!3}
!5 = !{i32 1, !4}
!6 = !{i32 11, i32 3}
!7 = !{!6}
!8 = !{i32 2, !7}
!9 = !{i32 11, i32 4}
!10 = !{!9}
!11 = !{i32 3, !10}
!12 = !{!2, !5, !8, !11}
!13 = !{i32 30, i32 0}
!14 = !{!13}
!15 = !{[584 x i8] c"\07\00\00\00\06\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\18\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\03\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\02\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\04\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\03\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\05\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00 \00\00\00\00\00\00\00\01\00\00\00\01\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"}

@ubo_buffer = global [48 x i8] zeroinitializer, align 4
declare i32 @printf(ptr, ...)
@fmt = constant [11 x i8] c"result=%f\0A\00"
define i32 @main() {
  call void @compute()
  ret i32 0
}
