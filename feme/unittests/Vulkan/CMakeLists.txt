set(LLVM_LINK_COMPONENTS
  Support
  )

add_feme_unittest(FeMeVulkanTests
  ObjectModelTest.cpp
  PhysicalDeviceInfoTest.cpp
  ProcAddrTest.cpp
  )
target_link_libraries(FeMeVulkanTests
  PRIVATE
  FeMeVulkanCore
  )
