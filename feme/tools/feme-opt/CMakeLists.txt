set(LLVM_LINK_COMPONENTS
  AsmParser
  BitWriter
  Core
  IRReader
  Passes
  Support
  )

set(LIBS
  MLIROptLib
  MLIRRegisterAllDialects
  MLIRRegisterAllPasses
  FeMeConversionSPIRVToLLVM
  FeMeDXSADialect
  FeMeTransformsAMDGPU
  FeMeTransformsDXIL
  FeMeTransformsSPIRV
  )

add_llvm_executable(feme-opt feme-opt.cpp)

llvm_update_compile_flags(feme-opt)
target_link_libraries(feme-opt PRIVATE ${LIBS})

mlir_check_all_link_libraries(feme-opt)
