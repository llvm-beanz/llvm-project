set(LLVM_LINK_COMPONENTS
  FuzzerCLI
  Support
  )

add_llvm_fuzzer(feme-dxbc-import-fuzzer
  feme-dxbc-import-fuzzer.cpp
  DUMMY_MAIN DummyImporterFuzzer.cpp
  )

mlir_target_link_libraries(feme-dxbc-import-fuzzer
  PUBLIC
  FeMeDXSADialect
  FeMeTargetDXSA
  MLIRIR
  )
