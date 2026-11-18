# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

function(feme_initialize_probe_tests)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(FEME_PROBE_LIT
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../llvm/utils/lit/lit.py"
    CACHE FILEPATH "LLVM LIT runner for the standalone Windows probes")
  if(NOT EXISTS "${FEME_PROBE_LIT}")
    message(FATAL_ERROR "Set FEME_PROBE_LIT to an existing LLVM lit.py.")
  endif()
  set(test_dir "${CMAKE_CURRENT_BINARY_DIR}/lit")
  # Reconfiguring a matrix must not retain tests for removed versions/options.
  file(GLOB stale_tests "${test_dir}/*/*.test")
  if(stale_tests)
    file(REMOVE ${stale_tests})
  endif()
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/lit.cfg.py.in"
    "${test_dir}/lit.cfg.py" @ONLY)
  file(GENERATE OUTPUT "${test_dir}/$<CONFIG>/lit.cfg.py"
    INPUT "${test_dir}/lit.cfg.py")
  add_custom_target(check-feme-windows-probes
    COMMAND "${Python3_EXECUTABLE}" "${FEME_PROBE_LIT}"
      -sv --timeout=30 "${test_dir}/$<CONFIG>"
    COMMENT "Running ${PROJECT_NAME} tests through LIT"
    USES_TERMINAL
    VERBATIM)
endfunction()

function(feme_add_probe_test)
  cmake_parse_arguments(TEST "WILL_FAIL" "NAME" "COMMAND" ${ARGN})
  if(NOT TEST_NAME OR NOT TEST_COMMAND OR TEST_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Expected NAME and COMMAND for a Windows probe LIT test.")
  endif()
  list(POP_FRONT TEST_COMMAND program)
  if(TARGET "${program}")
    add_dependencies(check-feme-windows-probes "${program}")
    set(program "$<TARGET_FILE:${program}>")
  endif()
  set(run "# RUN:")
  if(TEST_WILL_FAIL)
    string(APPEND run " not")
  endif()
  foreach(arg IN ITEMS "${program}" ${TEST_COMMAND})
    string(REGEX MATCHALL "\\$<TARGET_FILE:[^>]+>" target_files "${arg}")
    foreach(target_file IN LISTS target_files)
      string(REGEX REPLACE "^\\$<TARGET_FILE:([^>]+)>$" "\\1"
        target "${target_file}")
      add_dependencies(check-feme-windows-probes "${target}")
    endforeach()
    string(REPLACE "\\" "\\\\" arg "${arg}")
    string(REPLACE "\"" "\\\"" arg "${arg}")
    string(APPEND run " \"${arg}\"")
  endforeach()
  file(GENERATE
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/lit/$<CONFIG>/${TEST_NAME}.test"
    CONTENT "${run}\n")
endfunction()
