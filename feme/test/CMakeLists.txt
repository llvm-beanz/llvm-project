llvm_canonicalize_cmake_booleans()

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
  )

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/Unit/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/Unit/lit.cfg.py
  )

set(FEME_TEST_DEPENDS
  FileCheck count not
  clang
  dxbc-as
  dxbc-as-fuzzer
  feme
  feme-opt
  feme-translate
  feme-run
  feme-render
  feme-cfg-gen
  feme-dxil-import-fuzzer
  feme-dxbc-import-fuzzer
  feme-spirv-import-fuzzer
  feme-cpu-restructure-fuzzer
  FeMeUnitTests
  llc
  llvm-as
  llvm-nm
  llvm-readobj
  opt
  llvm-objcopy
  llvm-nm
  obj2yaml
  split-file
  yaml2obj
  )
if(FEME_HAVE_VULKAN_HEADERS)
  list(APPEND FEME_TEST_DEPENDS FeMeVulkanTests
    feme-vulkan-pipeline-cache-fuzzer)
endif()
if(FEME_HAVE_VULKAN_LOADER)
  # The loader-smoke and two-ICD-coexistence tests load `feme_vulkan`
  # through the real Vulkan loader via its build-tree manifest rather than
  # linking it directly, so it must be listed explicitly here to be built
  # before they run.
  list(APPEND FEME_TEST_DEPENDS feme_vulkan feme-vulkan-loader-smoke
    feme-vulkan-storage-buffer-diff feme-vulkan-image-loader-smoke
    feme-vulkan-sampled-image-smoke)
endif()

add_custom_target(feme-test-depends DEPENDS ${FEME_TEST_DEPENDS})

add_lit_testsuite(check-feme "Running the feme regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${FEME_TEST_DEPENDS}
  )
set_target_properties(check-feme PROPERTIES FOLDER "Tests")

add_lit_testsuites(FEME ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS ${FEME_TEST_DEPENDS})

# Roadmap step R6 (see feme/docs/Roadmap.md's §1.7 P0): a bounded,
# seed-corpus-only run of every fuzz target, so a fuzzer that stops
# compiling or regresses to crashing on one of its own checked-in seeds is
# caught by `ninja check-feme-fuzz` (a few seconds of test time) rather
# than only by an actual, unbounded fuzzing campaign that nothing today
# runs in CI.
set(FEME_FUZZ_TARGETS
  feme-dxil-import-fuzzer
  feme-dxbc-import-fuzzer
  feme-spirv-import-fuzzer
  dxbc-as-fuzzer
  feme-cpu-restructure-fuzzer
  )
if(FEME_HAVE_VULKAN_HEADERS)
  list(APPEND FEME_FUZZ_TARGETS feme-vulkan-pipeline-cache-fuzzer)
endif()

set(FEME_FUZZ_ARGS "")
foreach(fuzzer ${FEME_FUZZ_TARGETS})
  list(APPEND FEME_FUZZ_ARGS
    "--fuzzer=$<TARGET_FILE:${fuzzer}>=${FEME_SOURCE_DIR}/tools/${fuzzer}/seed-corpus")
endforeach()

add_custom_target(check-feme-fuzz
  COMMAND ${Python3_EXECUTABLE}
          ${FEME_SOURCE_DIR}/utils/check-feme-fuzz.py
          ${FEME_FUZZ_ARGS}
  DEPENDS ${FEME_FUZZ_TARGETS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running bounded seed-corpus fuzz checks"
  USES_TERMINAL
  )
set_target_properties(check-feme-fuzz PROPERTIES FOLDER "Tests")
