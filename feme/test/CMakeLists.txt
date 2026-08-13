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
  feme
  feme-opt
  feme-translate
  feme-run
  feme-cfg-gen
  FeMeUnitTests
  llc
  llvm-as
  llvm-nm
  llvm-readobj
  opt
  llvm-objcopy
  obj2yaml
  split-file
  yaml2obj
  )

add_custom_target(feme-test-depends DEPENDS ${FEME_TEST_DEPENDS})

add_lit_testsuite(check-feme "Running the feme regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${FEME_TEST_DEPENDS}
  )
set_target_properties(check-feme PROPERTIES FOLDER "Tests")

add_lit_testsuites(FEME ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS ${FEME_TEST_DEPENDS})
