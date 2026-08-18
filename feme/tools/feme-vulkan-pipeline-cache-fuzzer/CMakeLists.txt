set(LLVM_LINK_COMPONENTS
  FuzzerCLI
  Support
  )

add_llvm_fuzzer(feme-vulkan-pipeline-cache-fuzzer
  feme-vulkan-pipeline-cache-fuzzer.cpp
  DUMMY_MAIN DummyPipelineCacheFuzzer.cpp
  )

target_include_directories(feme-vulkan-pipeline-cache-fuzzer PRIVATE
  ${FEME_SOURCE_DIR}/lib/Vulkan)
target_link_libraries(feme-vulkan-pipeline-cache-fuzzer PRIVATE FeMeVulkanCore)
