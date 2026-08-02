set(LLVM_LINK_COMPONENTS
  BitReader
  FuzzerCLI
  Object
  Support
  )

add_llvm_fuzzer(feme-dxil-import-fuzzer
  feme-dxil-import-fuzzer.cpp
  DUMMY_MAIN DummyImporterFuzzer.cpp
  )

mlir_target_link_libraries(feme-dxil-import-fuzzer
  PUBLIC
  FeMeCore
  FeMeImportDXIL
  )
