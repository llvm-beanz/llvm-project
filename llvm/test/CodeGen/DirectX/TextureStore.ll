; RUN: opt -S -dxil-op-lower %s | FileCheck %s

target triple = "dxil-pc-shadermodel6.6-compute"

; CHECK-LABEL: define void @store_texture2d_float4(
define void @store_texture2d_float4(<2 x i32> %coords, <4 x float> %data) {
  %texture = call target("dx.Texture", <4 x float>, 1, 0, 0, 2)
      @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f32_1_0_0_2t(
          i32 0, i32 0, i32 1, i32 0, ptr null)

  ; CHECK: %[[COORD0:.*]] = extractelement <2 x i32> %coords, i64 0
  ; CHECK: %[[COORD1:.*]] = extractelement <2 x i32> %coords, i64 1
  ; CHECK: %[[DATA0:.*]] = extractelement <4 x float> %data, i32 0
  ; CHECK: %[[DATA1:.*]] = extractelement <4 x float> %data, i32 1
  ; CHECK: %[[DATA2:.*]] = extractelement <4 x float> %data, i32 2
  ; CHECK: %[[DATA3:.*]] = extractelement <4 x float> %data, i32 3
  ; CHECK: call void @dx.op.textureStore.f32(i32 67, %dx.types.Handle %{{.*}}, i32 %[[COORD0]], i32 %[[COORD1]], i32 undef, float %[[DATA0]], float %[[DATA1]], float %[[DATA2]], float %[[DATA3]], i8 15)
  call void @llvm.dx.resource.store.texture.tdx.Texture_v4f32_1_0_0_2t.v2i32.v4f32(
      target("dx.Texture", <4 x float>, 1, 0, 0, 2) %texture,
      <2 x i32> %coords, <4 x float> %data)

  ret void
}

; CHECK-LABEL: define void @store_texture2d_half4(
define void @store_texture2d_half4(<2 x i32> %coords, <4 x half> %data) {
  %texture = call target("dx.Texture", <4 x half>, 1, 0, 0, 2)
      @llvm.dx.resource.handlefrombinding.tdx.Texture_v4f16_1_0_0_2t(
          i32 0, i32 0, i32 1, i32 0, ptr null)

  ; CHECK: %[[COORD0:.*]] = extractelement <2 x i32> %coords, i64 0
  ; CHECK: %[[COORD1:.*]] = extractelement <2 x i32> %coords, i64 1
  ; CHECK: %[[DATA0:.*]] = extractelement <4 x half> %data, i32 0
  ; CHECK: %[[DATA1:.*]] = extractelement <4 x half> %data, i32 1
  ; CHECK: %[[DATA2:.*]] = extractelement <4 x half> %data, i32 2
  ; CHECK: %[[DATA3:.*]] = extractelement <4 x half> %data, i32 3
  ; CHECK: call void @dx.op.textureStore.f16(i32 67, %dx.types.Handle %{{.*}}, i32 %[[COORD0]], i32 %[[COORD1]], i32 undef, half %[[DATA0]], half %[[DATA1]], half %[[DATA2]], half %[[DATA3]], i8 15)
  call void @llvm.dx.resource.store.texture.tdx.Texture_v4f16_1_0_0_2t.v2i32.v4f16(
      target("dx.Texture", <4 x half>, 1, 0, 0, 2) %texture,
      <2 x i32> %coords, <4 x half> %data)

  ret void
}

; CHECK-LABEL: define void @store_texture1d_float(
define void @store_texture1d_float(i32 %coord, float %data) {
  %texture = call target("dx.Texture", float, 1, 0, 0, 1)
      @llvm.dx.resource.handlefrombinding.tdx.Texture_f32_1_0_0_1t(
          i32 0, i32 1, i32 1, i32 0, ptr null)

  ; CHECK: call void @dx.op.textureStore.f32(i32 67, %dx.types.Handle %{{.*}}, i32 %coord, i32 undef, i32 undef, float %data, float %data, float %data, float %data, i8 15)
  call void @llvm.dx.resource.store.texture.tdx.Texture_f32_1_0_0_1t.i32.f32(
      target("dx.Texture", float, 1, 0, 0, 1) %texture,
      i32 %coord, float %data)

  ret void
}

; CHECK-LABEL: define void @store_texture3d_float3(
define void @store_texture3d_float3(<3 x i32> %coords, <3 x float> %data) {
  %texture = call target("dx.Texture", <3 x float>, 1, 0, 0, 4)
      @llvm.dx.resource.handlefrombinding.tdx.Texture_v3f32_1_0_0_4t(
          i32 0, i32 2, i32 1, i32 0, ptr null)

  ; CHECK: %[[COORD0:.*]] = extractelement <3 x i32> %coords, i64 0
  ; CHECK: %[[COORD1:.*]] = extractelement <3 x i32> %coords, i64 1
  ; CHECK: %[[COORD2:.*]] = extractelement <3 x i32> %coords, i64 2
  ; CHECK: %[[DATA0:.*]] = extractelement <3 x float> %data, i32 0
  ; CHECK: %[[DATA1:.*]] = extractelement <3 x float> %data, i32 1
  ; CHECK: %[[DATA2:.*]] = extractelement <3 x float> %data, i32 2
  ; CHECK: call void @dx.op.textureStore.f32(i32 67, %dx.types.Handle %{{.*}}, i32 %[[COORD0]], i32 %[[COORD1]], i32 %[[COORD2]], float %[[DATA0]], float %[[DATA1]], float %[[DATA2]], float %[[DATA0]], i8 15)
  call void @llvm.dx.resource.store.texture.tdx.Texture_v3f32_1_0_0_4t.v3i32.v3f32(
      target("dx.Texture", <3 x float>, 1, 0, 0, 4) %texture,
      <3 x i32> %coords, <3 x float> %data)

  ret void
}
