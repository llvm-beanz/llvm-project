target triple = "dxil-unknown-shadermodel6.5-library"

define i32 @add(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  ret i32 %sum
}
