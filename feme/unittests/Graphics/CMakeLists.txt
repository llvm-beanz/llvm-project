set(LLVM_LINK_COMPONENTS
  Core
  Support
  )

add_feme_unittest(FeMeGraphicsTests
  PipelineTest.cpp
  PreparedDrawTest.cpp
  )
target_link_libraries(FeMeGraphicsTests
  PRIVATE
  FeMeGraphics
  )
