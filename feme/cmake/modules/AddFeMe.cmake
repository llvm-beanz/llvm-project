include(GNUInstallDirs)
include(LLVMDistributionSupport)

# add_feme_library(name sources...
#   SHARED
#   STATIC
#   INSTALL_WITH_TOOLCHAIN
#   DISABLE_INSTALL
#   ADDITIONAL_HEADERS header1...
#   DEPENDS target1...
#   LINK_COMPONENTS component1...
#   LINK_LIBS lib1...
#   )
#
# Declares a FeMe library, following the same pattern as add_llvm_library /
# add_mlir_library / add_clang_library: it forwards to llvm_add_library() so
# the resulting target honors LLVM's own build-configuration options
# (BUILD_SHARED_LIBS, LLVM_ENABLE_PIC/LTO, sanitizer instrumentation, unity
# builds, install rules, etc.), rather than hard-coding a library type and
# bypassing all of that via a bare add_library() call.
function(add_feme_library name)
  cmake_parse_arguments(ARG
    "SHARED;STATIC;INSTALL_WITH_TOOLCHAIN;DISABLE_INSTALL"
    ""
    "ADDITIONAL_HEADERS;DEPENDS;LINK_COMPONENTS;LINK_LIBS"
    ${ARGN})

  # Determine the type of library to build. An explicit SHARED/STATIC always
  # wins (llvm_add_library() otherwise defers to BUILD_SHARED_LIBS for it,
  # e.g. for FeMeVulkanCore, which unit tests must always link statically);
  # otherwise, also build an OBJECT library alongside so IDE generators
  # without proper object-library support (Xcode, Visual Studio) still see
  # per-file source lists instead of a single opaque archive/dylib target.
  if(ARG_SHARED AND ARG_STATIC)
    set(LIBTYPE SHARED STATIC)
  elseif(ARG_SHARED)
    set(LIBTYPE SHARED)
  elseif(ARG_STATIC)
    set(LIBTYPE STATIC)
  else()
    if(BUILD_SHARED_LIBS)
      set(LIBTYPE SHARED)
    else()
      set(LIBTYPE STATIC)
    endif()
    if(NOT XCODE AND NOT MSVC_IDE)
      list(APPEND LIBTYPE OBJECT)
    endif()
  endif()

  llvm_add_library(${name} ${LIBTYPE} ${ARG_UNPARSED_ARGUMENTS}
    ADDITIONAL_HEADERS ${ARG_ADDITIONAL_HEADERS}
    DEPENDS ${ARG_DEPENDS}
    LINK_COMPONENTS ${ARG_LINK_COMPONENTS}
    LINK_LIBS ${ARG_LINK_LIBS})

  if(TARGET ${name})
    target_link_libraries(${name} INTERFACE ${LLVM_COMMON_LIBS})
    if(ARG_INSTALL_WITH_TOOLCHAIN)
      set_target_properties(${name} PROPERTIES FEME_INSTALL_WITH_TOOLCHAIN TRUE)
    endif()
    if(NOT ARG_DISABLE_INSTALL)
      add_feme_library_install(${name})
    endif()
  else()
    # Add empty "phony" target, e.g. when llvm_add_library() skips a MODULE
    # that isn't supported on this platform.
    add_custom_target(${name})
  endif()
  set_target_properties(${name} PROPERTIES FOLDER "FeMe/Libraries")
endfunction(add_feme_library)

# Installs a FeMe library target, matching add_mlir_library_install's
# pattern. Left as its own function (rather than folded into
# add_feme_library) so a future standalone (out-of-tree) build -- currently
# out of scope, see feme/docs/Design.md -- can reuse it once it also adds the
# `install(EXPORT FeMeTargets ...)`/FeMeConfig.cmake plumbing that finalizes
# the export set this registers targets into.
function(add_feme_library_install name)
  get_target_property(_install_with_toolchain ${name} FEME_INSTALL_WITH_TOOLCHAIN)
  if(NOT LLVM_INSTALL_TOOLCHAIN_ONLY OR _install_with_toolchain)
    get_target_export_arg(${name} FeMe export_to_femetargets)
    install(TARGETS ${name}
      COMPONENT ${name}
      ${export_to_femetargets}
      LIBRARY DESTINATION lib${LLVM_LIBDIR_SUFFIX}
      ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX}
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
      OBJECTS DESTINATION lib${LLVM_LIBDIR_SUFFIX}
    )
    if(NOT LLVM_ENABLE_IDE)
      add_llvm_install_targets(install-${name}
                               DEPENDS ${name}
                               COMPONENT ${name})
    endif()
    set_property(GLOBAL APPEND PROPERTY FEME_ALL_LIBS ${name})
  endif()
  set_property(GLOBAL APPEND PROPERTY FEME_EXPORTS ${name})
endfunction()
