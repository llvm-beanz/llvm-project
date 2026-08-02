llvm_canonicalize_cmake_booleans()

configure_lit_site_cfg(
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
  ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py
  MAIN_CONFIG
  ${CMAKE_CURRENT_SOURCE_DIR}/lit.cfg.py
  )

set(FEME_TEST_DEPENDS
  FileCheck count not
  feme
  feme-opt
  feme-translate
  )

add_lit_testsuite(check-feme "Running the feme regression tests"
  ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS ${FEME_TEST_DEPENDS}
  )
set_target_properties(check-feme PROPERTIES FOLDER "Tests")

add_lit_testsuites(FEME ${CMAKE_CURRENT_SOURCE_DIR} DEPENDS ${FEME_TEST_DEPENDS})
