set(LLVM_LINK_COMPONENTS
  FuzzerCLI
  Support
  )

add_llvm_fuzzer(feme-spirv-import-fuzzer
  feme-spirv-import-fuzzer.cpp
  DUMMY_MAIN DummyImporterFuzzer.cpp
  )

mlir_target_link_libraries(feme-spirv-import-fuzzer
  PUBLIC
  FeMeCore
  FeMeImportSPIRV
  MLIRIR
  )
