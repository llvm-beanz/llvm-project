set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeImageTests
  BCDecodeTest.cpp
  BlockGeometryTest.cpp
  SubresourceLayoutTest.cpp
  )
target_link_libraries(FeMeImageTests
  PRIVATE
  FeMeImage
  )
