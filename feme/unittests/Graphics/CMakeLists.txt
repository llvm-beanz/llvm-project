set(LLVM_LINK_COMPONENTS
  Core
  Support
  TestingSupport
  )

add_feme_unittest(FeMeGraphicsTests
  ExecutorTest.cpp
  ImageFixtureTest.cpp
  PipelineTest.cpp
  PreparedDrawTest.cpp
  SceneTest.cpp
  )
target_link_libraries(FeMeGraphicsTests
  PRIVATE
  FeMeGraphics
  FeMeTransformsDXIL
  )
