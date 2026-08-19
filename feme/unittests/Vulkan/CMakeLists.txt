set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeVulkanTests
  BufferTest.cpp
  CommandBufferTest.cpp
  DescriptorTest.cpp
  DiagnosticsTest.cpp
  DrawTest.cpp
  FormatTest.cpp
  GraphicsPipelineTest.cpp
  GroupSizeTest.cpp
  ImageOpsTest.cpp
  ImageTest.cpp
  MemoryTest.cpp
  ObjectModelTest.cpp
  PhysicalDeviceInfoTest.cpp
  PipelineTest.cpp
  PipelineCacheTest.cpp
  ProcAddrTest.cpp
  RenderPassTest.cpp
  SyncTest.cpp
  )
target_link_libraries(FeMeVulkanTests
  PRIVATE
  FeMeVulkanCore
  MLIRSPIRVSerialization
  MLIRParser
  )

if (APPLE)
  get_filename_component(_Vulkan_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
  set_property(TARGET FeMeVulkanTests APPEND_STRING PROPERTY
               LINK_FLAGS " -Wl,-rpath,${_Vulkan_LIB_DIR} ")
endif()
