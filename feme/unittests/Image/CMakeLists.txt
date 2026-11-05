set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeImageTests
  BlockGeometryTest.cpp
  )
target_link_libraries(FeMeImageTests
  PRIVATE
  FeMeImage
  )
