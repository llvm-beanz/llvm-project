set(LLVM_LINK_COMPONENTS
  Support
  )

add_llvm_executable(feme-cfg-gen
  feme-cfg-gen.cpp
  )

llvm_update_compile_flags(feme-cfg-gen)
target_link_libraries(feme-cfg-gen PRIVATE FeMeTransformsCPU)
