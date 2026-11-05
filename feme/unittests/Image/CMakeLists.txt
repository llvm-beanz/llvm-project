set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeImageTests
  BC6HDecodeTest.cpp
  BC7DecodeTest.cpp
  BCDecodeTest.cpp
  BlockGeometryTest.cpp
  CopyWalkTest.cpp
  SubresourceLayoutTest.cpp
  ViewMaterializationTest.cpp
  )
target_link_libraries(FeMeImageTests
  PRIVATE
  FeMeImage
  )
