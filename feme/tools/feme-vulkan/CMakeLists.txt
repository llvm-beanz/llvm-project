# libfeme_vulkan: the FeMe Vulkan ICD's loader-facing shared object (see
# feme/docs/FeMeVulkanDesign.md, "V0: Loader-visible skeleton" and
# Roadmap.md's V0), built out of `tools` following the same convention as
# LLVM's other deliverable tools/libraries. Uses `add_feme_library(...
# SHARED ...)` (rather than a bare `add_library(... SHARED ...)`) so it
# still honors LLVM's own component-based build and install logic (e.g.
# BUILD_SHARED_LIBS, LTO, sanitizer instrumentation, and the standard
# `install(TARGETS ...)` rules), while the hidden-visibility/symbol-export
# packaging "Process Coexistence and Symbol Visibility" requires is layered
# on top via LLVM_EXPORTED_SYMBOL_FILE below.
#
# libfeme_vulkan.exports lists only the loader-facing entrypoints; every
# other symbol -- LLVM, MLIR, and FeMe's own -- must stay hidden so this ICD
# can share a process with another LLVM-based Vulkan driver without
# colliding global state. Setting LLVM_EXPORTED_SYMBOL_FILE before calling
# add_feme_library() (which forwards it to llvm_add_library()) reuses
# add_llvm_symbol_exports()'s existing per-platform handling -- a generated
# `--version-script` on ELF, `-exported_symbols_list` (with the leading
# underscore each symbol needs there) on Darwin, `.def`/`-bE:` on
# Windows/AIX -- rather than hard-coding the GNU-ld-only
# `-Wl,--version-script=` flag, which Apple's linker rejects outright.
set(LLVM_EXPORTED_SYMBOL_FILE
  ${CMAKE_CURRENT_SOURCE_DIR}/libfeme_vulkan.exports)
add_feme_library(feme_vulkan SHARED
  VulkanICD.cpp

  LINK_LIBS PRIVATE
  FeMeVulkanCore
  )
set_target_properties(feme_vulkan PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  PREFIX "lib"
  OUTPUT_NAME "feme_vulkan"
  )
# add_feme_library()/llvm_add_library() otherwise gives every SHARED target
# it builds on UNIX an LLVM-release-versioned SONAME/filename (e.g.
# "libfeme_vulkan.so.<version>"), the convention its own dylibs use.
# `feme_vulkan` is a Vulkan ICD, loaded by an absolute build-tree or
# installed manifest path (see "Loader Integration" below) rather than by
# soname, and negotiates its own version via the loader-driver interface
# (`vk_icdNegotiateLoaderICDInterfaceVersion` in VulkanICD.cpp); clearing
# these properties keeps the plain name "libfeme_vulkan.so".
set_property(TARGET feme_vulkan PROPERTY VERSION)
set_property(TARGET feme_vulkan PROPERTY SOVERSION)

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
