# libfeme_vulkan: the FeMe Vulkan ICD (see feme/docs/FeMeVulkanDesign.md,
# "V0: Loader-visible skeleton" and Roadmap.md's V0). Built as a genuine
# `SHARED` library (not through `add_llvm_library`/`add_mlir_library`, which
# assume LLVM's own component/dylib conventions) so this CMakeLists.txt has
# full control over hidden visibility and the exported-symbol version script
# "Process Coexistence and Symbol Visibility" requires.

set(feme_vulkan_gen_dir ${CMAKE_CURRENT_BINARY_DIR})
set(feme_vulkan_entrypoints_inc
  ${feme_vulkan_gen_dir}/VulkanEntrypoints.inc)
set(feme_vulkan_implemented
  ${CMAKE_CURRENT_SOURCE_DIR}/ImplementedEntrypoints.txt)

# Vulkan-Headers installs vk.xml at `<prefix>/share/vulkan/registry/vk.xml`,
# a sibling of the `<prefix>/include` directory `Vulkan::Headers` exposes;
# `FEME_VULKAN_XML` lets a build whose Vulkan-Headers doesn't follow that
# layout (e.g. a bare source checkout) point at it directly.
if(NOT FEME_VULKAN_XML)
  get_target_property(feme_vulkan_headers_include_dir Vulkan::Headers
    INTERFACE_INCLUDE_DIRECTORIES)
  get_filename_component(feme_vulkan_headers_prefix
    ${feme_vulkan_headers_include_dir} DIRECTORY)
  set(FEME_VULKAN_XML
    ${feme_vulkan_headers_prefix}/share/vulkan/registry/vk.xml
    CACHE FILEPATH "Path to Vulkan-Headers' vk.xml")
endif()
if(NOT EXISTS ${FEME_VULKAN_XML})
  message(FATAL_ERROR
    "feme: vk.xml not found at '${FEME_VULKAN_XML}'; set -DFEME_VULKAN_XML=<path>")
endif()

add_custom_command(
  OUTPUT ${feme_vulkan_entrypoints_inc}
  COMMAND ${Python3_EXECUTABLE}
          ${FEME_SOURCE_DIR}/utils/vk_gen_entrypoints.py
          ${FEME_VULKAN_XML} ${feme_vulkan_entrypoints_inc}
          --implemented ${feme_vulkan_implemented}
  DEPENDS ${FEME_SOURCE_DIR}/utils/vk_gen_entrypoints.py
          ${feme_vulkan_implemented} ${FEME_VULKAN_XML}
  COMMENT "Generating FeMe Vulkan ICD entrypoint table"
  VERBATIM
  )
add_custom_target(FeMeVulkanEntrypointsGen DEPENDS ${feme_vulkan_entrypoints_inc})

# The ICD's logic lives in a static library, `FeMeVulkanCore`, separate from
# the `feme_vulkan` shared object: unit tests link this directly so they can
# call `feme::vulkan::vkCreate*`/`ProcAddr.h`'s helpers without going through
# a dynamically-loaded ICD at all, while `feme_vulkan` below adds nothing but
# the loader-facing `vk_icd*` exports and this target's hidden-visibility/
# version-script packaging. Visibility attributes only govern a *shared*
# object's dynamic symbol table, so building this static library with the
# same hidden-by-default flags has no effect on the unit tests linking it
# directly into their own executable.
add_library(FeMeVulkanCore STATIC
  EntryPoints.cpp
  PhysicalDeviceInfo.cpp
  ProcAddr.cpp
  )
add_dependencies(FeMeVulkanCore FeMeVulkanEntrypointsGen)
target_include_directories(FeMeVulkanCore PUBLIC
  ${feme_vulkan_gen_dir}
  ${CMAKE_CURRENT_SOURCE_DIR}
  )
target_link_libraries(FeMeVulkanCore PUBLIC
  Vulkan::Headers
  FeMeTargetCPU
  )

add_library(feme_vulkan SHARED VulkanICD.cpp)
target_link_libraries(feme_vulkan PRIVATE FeMeVulkanCore)
set_target_properties(feme_vulkan PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  PREFIX "lib"
  OUTPUT_NAME "feme_vulkan"
  )
target_link_options(feme_vulkan PRIVATE
  "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libfeme_vulkan.map"
  )
set_target_properties(feme_vulkan PROPERTIES
  LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/libfeme_vulkan.map)

# The build-tree development manifest ("Loader Integration"): an absolute
# path into the build directory, usable via `VK_DRIVER_FILES` without
# installing anything. `file(GENERATE ...)` (rather than `configure_file`)
# so `$<TARGET_FILE:feme_vulkan>` -- the actual built shared library path --
# can be substituted; `configure_file` cannot evaluate generator
# expressions.
set(FEME_VULKAN_ICD_MANIFEST
  ${CMAKE_CURRENT_BINARY_DIR}/feme_icd.json
  CACHE INTERNAL "Path to the FeMe Vulkan ICD's build-tree development manifest")
file(GENERATE
  OUTPUT ${FEME_VULKAN_ICD_MANIFEST}
  CONTENT "{
  \"file_format_version\": \"1.0.1\",
  \"ICD\": {
    \"library_path\": \"$<TARGET_FILE:feme_vulkan>\",
    \"api_version\": \"1.1.0\",
    \"is_portability_driver\": false
  }
}
"
  )
