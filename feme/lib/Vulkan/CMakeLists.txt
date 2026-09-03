# FeMeVulkanCore: the FeMe Vulkan ICD's object model, entrypoint table, and
# `feme::vulkan::vk*` implementations (see feme/docs/FeMeVulkanDesign.md,
# "V0: Loader-visible skeleton" and Roadmap.md's V0). The loader-facing
# `feme_vulkan` shared object that links this static library lives under
# feme/tools/feme-vulkan, following LLVM's convention of building deliverable
# tools/libraries out of a `tools` directory; only what that target's build
# needs directly (its own source and symbol-export list) lives there, while the
# entrypoint-table generation below is shared with FeMeVulkanCore's own
# build here.
set(feme_vulkan_gen_dir ${CMAKE_CURRENT_BINARY_DIR})
set(feme_vulkan_entrypoints_inc
  ${feme_vulkan_gen_dir}/VulkanEntrypoints.inc)
set(feme_vulkan_implemented
  ${CMAKE_CURRENT_SOURCE_DIR}/ImplementedEntrypoints.txt)
# `VK_KHR_xcb_surface`'s own two commands (roadmap H10a): kept in a
# separate `--implemented` file, folded into the generator invocation only
# when `FEME_HAVE_XCB` is actually on, rather than merged into
# `ImplementedEntrypoints.txt` unconditionally -- see
# `vk_gen_entrypoints.py`'s own `--extra-extension` comment on why
# `SUPPORTED_EXTENSIONS` must not claim an extension this particular build
# does not genuinely implement.
set(feme_vulkan_implemented_xcb
  ${CMAKE_CURRENT_SOURCE_DIR}/ImplementedEntrypointsXcb.txt)
set(feme_vulkan_entrypoints_gen_args
  --implemented ${feme_vulkan_implemented})
set(feme_vulkan_entrypoints_gen_deps ${feme_vulkan_implemented})
if(FEME_HAVE_XCB)
  list(APPEND feme_vulkan_entrypoints_gen_args
    --extra-extension VK_KHR_xcb_surface
    --implemented ${feme_vulkan_implemented_xcb})
  list(APPEND feme_vulkan_entrypoints_gen_deps ${feme_vulkan_implemented_xcb})
endif()

# A Vulkan SDK installs vk.xml at `<prefix>/share/vulkan/registry/vk.xml`, a
# sibling of the `<prefix>/include` directory `find_package(Vulkan)` records
# as `Vulkan_INCLUDE_DIR`; `FEME_VULKAN_XML` lets a build whose Vulkan SDK
# doesn't follow that layout point at it directly.
if(NOT FEME_VULKAN_XML)
  get_filename_component(feme_vulkan_prefix ${Vulkan_INCLUDE_DIR} DIRECTORY)
  set(FEME_VULKAN_XML
    ${feme_vulkan_prefix}/share/vulkan/registry/vk.xml
    CACHE FILEPATH "Path to the Vulkan SDK's vk.xml")
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
          ${feme_vulkan_entrypoints_gen_args}
  DEPENDS ${FEME_SOURCE_DIR}/utils/vk_gen_entrypoints.py
          ${feme_vulkan_entrypoints_gen_deps} ${FEME_VULKAN_XML}
  COMMENT "Generating FeMe Vulkan ICD entrypoint table"
  VERBATIM
  )
add_custom_target(FeMeVulkanEntrypointsGen DEPENDS ${feme_vulkan_entrypoints_inc})

# The ICD's logic lives in a static library, `FeMeVulkanCore`, separate from
# the `feme_vulkan` shared object: unit tests link this directly so they can
# call `feme::vulkan::vkCreate*`/`ProcAddr.h`'s helpers without going through
# a dynamically-loaded ICD at all, while `feme_vulkan` below adds nothing but
# the loader-facing `vk_icd*` exports and this target's hidden-visibility/
# symbol-export packaging. Visibility attributes only govern a *shared*
# object's dynamic symbol table, so building this static library with the
# same hidden-by-default flags has no effect on the unit tests linking it
# directly into their own executable. Built via `add_feme_library(... STATIC
# ...)` (rather than a bare `add_library(... STATIC ...)`) so it still picks
# up LLVM's build-configuration options (e.g. LTO, sanitizer
# instrumentation); STATIC is passed explicitly since, unlike `feme_vulkan`
# below, this must never become a shared object even when BUILD_SHARED_LIBS
# is set, given the unit tests linking it directly. It is never installed
# (DISABLE_INSTALL): it exists solely to be linked into `feme_vulkan` and the
# unit tests below, not as a standalone deliverable.
#
# PhysicalDeviceInfo.cpp calls directly into these two components
# (`llvm::Expected`/`consumeError` and `llvm::MD5` from Support;
# `getHostCPUFeatures`/`getProcessTriple`/`getHostCPUName`/`Triple` from
# TargetParser), but FeMeVulkanCore never declared that dependency itself --
# it only ever got Support and TargetParser transitively through
# FeMeTargetCPU, leaving the link order these two static archives need
# relative to `libFeMeVulkanCore.a` entirely up to whatever else FeMeTargetCPU
# (or a given link job's other libraries) happened to require. That is not
# safe with a single-pass linker (e.g. Apple's classic `ld64`): if nothing
# else in the link needs a symbol from `Error.cpp`'s object file by the time
# the linker reaches it, `libLLVMSupport.a` is never re-scanned once
# `libFeMeVulkanCore.a`'s own need for it (e.g. the `ErrorInfo<ErrorList,
# ErrorInfoBase>` RTTI that `llvm::consumeError` pulls in) appears later on
# the command line, producing an undefined "typeinfo for
# llvm::ErrorInfoBase". Declaring the dependency directly (via
# LINK_COMPONENTS below) lets CMake's own library ordering account for it no
# matter what FeMeTargetCPU or anything else in a given link needs.
add_feme_library(FeMeVulkanCore STATIC
  ASTCDecode.cpp
  BC6HDecode.cpp
  BC7Decode.cpp
  BCDecode.cpp
  BCPartitionTables.cpp
  BCSamplingBridge.cpp
  Buffer.cpp
  CommandBuffer.cpp
  Descriptor.cpp
  Diagnostics.cpp
  EntryPoints.cpp
  ETC2Decode.cpp
  ETC2SamplingBridge.cpp
  Format.cpp
  GraphicsPipeline.cpp
  GroupSize.cpp
  HostImageCopy.cpp
  Image.cpp
  ImageOps.cpp
  Memory.cpp
  PhysicalDeviceInfo.cpp
  Pipeline.cpp
  PipelineCache.cpp
  PrivateData.cpp
  ProcAddr.cpp
  QueryPool.cpp
  RenderPass.cpp
  Surface.cpp
  Swapchain.cpp
  Sync.cpp
  XcbSurface.cpp

  DISABLE_INSTALL

  LINK_COMPONENTS
  Support
  TargetParser

  LINK_LIBS PUBLIC
  Vulkan::Headers
  FeMeCore
  FeMeGraphics
  FeMeImportSPIRV
  FeMeTargetCPU
  FeMeTransformsGraphics
  FeMeTranslateSPIRV
  )
add_dependencies(FeMeVulkanCore FeMeVulkanEntrypointsGen)
target_include_directories(FeMeVulkanCore PUBLIC
  ${feme_vulkan_gen_dir}
  ${CMAKE_CURRENT_SOURCE_DIR}
  )
if(FEME_VULKAN_TRUST_PIPELINE_CACHE_DATA)
  target_compile_definitions(FeMeVulkanCore PRIVATE
    FEME_VULKAN_TRUST_PIPELINE_CACHE_DATA=1)
else()
  target_compile_definitions(FeMeVulkanCore PRIVATE
    FEME_VULKAN_TRUST_PIPELINE_CACHE_DATA=0)
endif()

# Roadmap H10a: `VK_KHR_xcb_surface`'s implementation (`XcbSurface.cpp`) is
# always compiled, guarding its own body internally, so this definition
# must be PUBLIC -- `EntryPoints.h` uses it to decide whether to declare
# the two xcb-specific entry points at all (see that header's own comment),
# and every TU that includes it, including consumers linking
# FeMeVulkanCore rather than building inside it (unit tests, and the
# `feme_vulkan` shared object built from feme/tools/feme-vulkan), must
# agree with FeMeVulkanCore's own build on this macro's value.
if(FEME_HAVE_XCB)
  target_compile_definitions(FeMeVulkanCore PUBLIC FEME_VULKAN_HAVE_XCB=1)
  target_include_directories(FeMeVulkanCore PUBLIC ${FEME_XCB_INCLUDE_DIR})
  target_link_libraries(FeMeVulkanCore PUBLIC ${FEME_XCB_LIBRARY})
else()
  target_compile_definitions(FeMeVulkanCore PUBLIC FEME_VULKAN_HAVE_XCB=0)
endif()

