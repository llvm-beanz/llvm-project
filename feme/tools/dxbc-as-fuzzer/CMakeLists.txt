set(LLVM_LINK_COMPONENTS
  BinaryFormat
  FuzzerCLI
  Support
  )

add_llvm_fuzzer(dxbc-as-fuzzer
  dxbc-as-fuzzer.cpp
  DUMMY_MAIN DummyFuzzer.cpp
  )

target_link_libraries(dxbc-as-fuzzer PRIVATE FeMeDXBCAssembler)
