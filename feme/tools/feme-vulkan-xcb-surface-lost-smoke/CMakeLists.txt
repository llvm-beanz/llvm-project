# feme-vulkan-xcb-surface-lost-smoke: a tiny Vulkan client, linked against
# the real Khronos Vulkan loader and a real xcb connection, used by
# feme/test/Vulkan's roadmap H10g lit test to deterministically reproduce a
# real `xcb_get_geometry` failure (a destroyed window's real `BadWindow`
# protocol error) and confirm `vkGetPhysicalDeviceSurfaceCapabilitiesKHR`
# (Surface.cpp) reports it as `VK_ERROR_SURFACE_LOST_KHR`, not the same
# `{UINT32_MAX, UINT32_MAX}`/`VK_SUCCESS` a headless surface's legitimate
# "undefined size" answer gives. Deliberately plain CMake, not
# `add_llvm_executable`, matching feme-vulkan-xcb-smoke's own precedent: a
# Vulkan application, not an LLVM tool, so it must not link LLVM/MLIR at all.
add_executable(feme-vulkan-xcb-surface-lost-smoke
  feme-vulkan-xcb-surface-lost-smoke.cpp
  )
target_link_libraries(feme-vulkan-xcb-surface-lost-smoke PRIVATE
  Vulkan::Headers
  Vulkan::Vulkan
  ${FEME_XCB_LIBRARY}
  )
target_include_directories(feme-vulkan-xcb-surface-lost-smoke PRIVATE
  ${FEME_XCB_INCLUDE_DIR}
  )

if (APPLE)
  get_filename_component(_Vulkan_LIB_DIR ${Vulkan_LIBRARY} DIRECTORY)
  set_property(TARGET feme-vulkan-xcb-surface-lost-smoke APPEND_STRING PROPERTY
               LINK_FLAGS " -Wl,-rpath,${_Vulkan_LIB_DIR} ")
endif()
