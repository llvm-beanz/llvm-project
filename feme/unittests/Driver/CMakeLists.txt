set(LLVM_LINK_COMPONENTS
  BitReader
  BitWriter
  Core
  Object
  ObjectYAML
  Support
  TestingSupport
  )

add_feme_unittest(FeMeDriverTests
  DriverTest.cpp
  )
target_link_libraries(FeMeDriverTests
  PRIVATE
  FeMeCore
  FeMeDriver
  )
