# feme-vulkan-graphics-smoke: a tiny Vulkan client linked against the real
# Khronos Vulkan loader, used by feme/test/Vulkan's V6 off-screen rendering
# lit test (see "V6: Graphics queue and basic rendering" in
# feme/docs/FeMeVulkanDesign.md).
# Deliberately plain CMake, not `add_llvm_executable`: it is a Vulkan
# application, not an LLVM tool, and must not link LLVM/MLIR at all.
add_executable(feme-vulkan-graphics-smoke
  feme-vulkan-graphics-smoke.cpp
  )
target_link_libraries(feme-vulkan-graphics-smoke PRIVATE
  Vulkan::Headers
  Vulkan::Vulkan
  )

if (APPLE)
  get_filename_component(_Vulkan_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
  set_property(TARGET feme-vulkan-graphics-smoke APPEND_STRING PROPERTY
               LINK_FLAGS " -Wl,-rpath,${_Vulkan_LIB_DIR} ")
endif()
