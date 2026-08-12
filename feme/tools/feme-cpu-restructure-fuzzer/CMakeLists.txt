set(LLVM_LINK_COMPONENTS
  AsmParser
  Core
  FuzzerCLI
  Support
  )

add_llvm_fuzzer(feme-cpu-restructure-fuzzer
  feme-cpu-restructure-fuzzer.cpp
  DUMMY_MAIN DummyRestructureFuzzer.cpp
  )

target_link_libraries(feme-cpu-restructure-fuzzer PRIVATE FeMeTransformsCPU)
