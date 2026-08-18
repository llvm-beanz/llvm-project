# feme-vulkan-storage-buffer-diff: a tiny Vulkan client linked against the
# real Khronos Vulkan loader, used by feme/test/Vulkan's lavapipe
# differential test (see "V2: Storage buffers and descriptors" in
# feme/docs/FeMeVulkanDesign.md). Deliberately plain CMake, not
# `add_llvm_executable`: it is a Vulkan application, not an LLVM tool, and
# must not link LLVM/MLIR at all.
add_executable(feme-vulkan-storage-buffer-diff
  feme-vulkan-storage-buffer-diff.cpp
  )
target_link_libraries(feme-vulkan-storage-buffer-diff PRIVATE
  Vulkan::Headers
  Vulkan::Vulkan
  )

if (APPLE)
  get_filename_component(_Vulkan_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
  set_property(TARGET feme-vulkan-storage-buffer-diff APPEND_STRING PROPERTY
               LINK_FLAGS " -Wl,-rpath,${_Vulkan_LIB_DIR} ")
endif()
