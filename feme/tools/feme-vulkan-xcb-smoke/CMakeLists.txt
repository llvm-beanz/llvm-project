# feme-vulkan-xcb-smoke: a tiny Vulkan client linked against the real
# Khronos Vulkan loader and a real xcb connection, used by
# feme/test/Vulkan's roadmap H10a lit test (see "Window-system
# integration" in feme/docs/FeMeVulkanDesign.md). Only built when
# `FEME_HAVE_XCB` is on (see feme/CMakeLists.txt and
# feme/tools/CMakeLists.txt) -- unlike `XcbSurface.cpp`'s own internal
# `#if`/`#else` split inside FeMeVulkanCore, this client tool has no stub
# to fall back to (there is nothing useful for it to test without a real
# xcb connection), so it is simply not built at all in that configuration.
# Deliberately plain CMake, not `add_llvm_executable`: it is a Vulkan
# application, not an LLVM tool, and must not link LLVM/MLIR at all.
add_executable(feme-vulkan-xcb-smoke
  feme-vulkan-xcb-smoke.cpp
  )
target_link_libraries(feme-vulkan-xcb-smoke PRIVATE
  Vulkan::Headers
  Vulkan::Vulkan
  ${FEME_XCB_LIBRARY}
  )
target_include_directories(feme-vulkan-xcb-smoke PRIVATE
  ${FEME_XCB_INCLUDE_DIR}
  )

if (APPLE)
  get_filename_component(_Vulkan_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
  set_property(TARGET feme-vulkan-xcb-smoke APPEND_STRING PROPERTY
               LINK_FLAGS " -Wl,-rpath,${_Vulkan_LIB_DIR} ")
endif()
