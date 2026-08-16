set(LLVM_LINK_COMPONENTS
  Core
  Support
  TestingSupport
  )

add_feme_unittest(FeMeGraphicsTests
  ImageFixtureTest.cpp
  PipelineTest.cpp
  PreparedDrawTest.cpp
  )
target_link_libraries(FeMeGraphicsTests
  PRIVATE
  FeMeGraphics
  )
