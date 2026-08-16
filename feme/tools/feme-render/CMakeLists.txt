set(LLVM_LINK_COMPONENTS
  AsmParser
  BitReader
  Core
  IRReader
  Support
  native
  )

add_llvm_executable(feme-render feme-render.cpp)

llvm_update_compile_flags(feme-render)
target_link_libraries(feme-render PRIVATE FeMeCore FeMeGraphics FeMeTargetCPU
                                          FeMeTransformsDXIL)
