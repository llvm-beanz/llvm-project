set(LLVM_LINK_COMPONENTS
  AsmParser
  BitReader
  Core
  IRReader
  Support
  native
  )

add_llvm_executable(feme-run feme-run.cpp)

llvm_update_compile_flags(feme-run)
target_link_libraries(feme-run PRIVATE FeMeCore FeMeImportDXIL FeMeTargetCPU
                                        FeMeTransformsDXIL)
