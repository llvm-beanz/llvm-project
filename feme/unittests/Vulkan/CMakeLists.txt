set(LLVM_LINK_COMPONENTS
  Support
  TestingSupport
  )

add_feme_unittest(FeMeVulkanTests
  ASTCDecodeTest.cpp
  BC6HDecodeTest.cpp
  BC7DecodeTest.cpp
  BCDecodeTest.cpp
  BufferTest.cpp
  CommandBufferTest.cpp
  DescriptorTest.cpp
  DiagnosticsTest.cpp
  DrawTest.cpp
  EntryPointsTest.cpp
  ETC2DecodeTest.cpp
  FormatTest.cpp
  GraphicsPipelineTest.cpp
  GroupSizeTest.cpp
  HostImageCopyTest.cpp
  ImageOpsTest.cpp
  ImageTest.cpp
  MemoryTest.cpp
  ObjectModelTest.cpp
  PhysicalDeviceInfoTest.cpp
  PipelineTest.cpp
  PipelineCacheTest.cpp
  PrivateDataTest.cpp
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
