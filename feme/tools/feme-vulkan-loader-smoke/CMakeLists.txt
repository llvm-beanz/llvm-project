# feme-vulkan-loader-smoke: a tiny Vulkan client linked against the real
# Khronos Vulkan loader, used by feme/test/Vulkan's lit tests (see "V0:
# Loader-visible skeleton" in feme/docs/FeMeVulkanDesign.md). Deliberately
# plain CMake, not `add_llvm_executable`: it is a Vulkan application, not an
# LLVM tool, and must not link LLVM/MLIR at all.
add_executable(feme-vulkan-loader-smoke
  feme-vulkan-loader-smoke.cpp
  )
target_link_libraries(feme-vulkan-loader-smoke PRIVATE
  Vulkan::Headers
  ${FEME_VULKAN_LOADER_LIBRARY}
  )
