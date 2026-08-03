# -*- Python -*-

# Configuration file for the 'lit' test runner, driving FeMe's gtest-based
# unit tests through the same `check-feme` entry point as the lit/FileCheck
# tests in feme/test, mirroring llvm/test/Unit and clang/test/Unit.

import os

import lit.formats

# name: The name of this test suite.
config.name = "FeMe-Unit"

# suffixes: A list of file extensions to treat as test files.
config.suffixes = []

# test_source_root: The root path where tests are located.
# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(config.feme_obj_root, "unittests")
config.test_source_root = config.test_exec_root

# testFormat: The test format to use to interpret tests.
config.test_format = lit.formats.GoogleTest(config.llvm_build_mode, "Tests")

# Propagate the temp directory. Windows requires this because it uses \Windows\
# if none of these are present.
for var in ["TMP", "TEMP", "HOME", "SYSTEMDRIVE"]:
    if var in os.environ:
        config.environment[var] = os.environ[var]

# Propagate sanitizer options.
for var in [
    "ASAN_SYMBOLIZER_PATH",
    "HWASAN_SYMBOLIZER_PATH",
    "MSAN_SYMBOLIZER_PATH",
    "TSAN_SYMBOLIZER_PATH",
    "UBSAN_SYMBOLIZER_PATH",
    "ASAN_OPTIONS",
    "HWASAN_OPTIONS",
    "MSAN_OPTIONS",
    "TSAN_OPTIONS",
    "UBSAN_OPTIONS",
]:
    if var in os.environ:
        config.environment[var] = os.environ[var]
