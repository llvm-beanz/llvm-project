# feme-vulkan-sampled-image-smoke: a tiny Vulkan client linked against the
# real Khronos Vulkan loader, used by feme/test/Vulkan's sampled-image lit
# test (see "V5: Images and sampling" in feme/docs/FeMeVulkanDesign.md and
# roadmap R30's SPIR-V image lowering, which is what lets a dispatch consume
# an image at all). Deliberately plain CMake, not `add_llvm_executable`: it
# is a Vulkan application, not an LLVM tool, and must not link LLVM/MLIR at
# all.
add_executable(feme-vulkan-sampled-image-smoke
  feme-vulkan-sampled-image-smoke.cpp
  )
target_link_libraries(feme-vulkan-sampled-image-smoke PRIVATE
  Vulkan::Headers
  Vulkan::Vulkan
  )

if (APPLE)
  get_filename_component(_Vulkan_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
  set_property(TARGET feme-vulkan-sampled-image-smoke APPEND_STRING PROPERTY
               LINK_FLAGS " -Wl,-rpath,${_Vulkan_LIB_DIR} ")
endif()
