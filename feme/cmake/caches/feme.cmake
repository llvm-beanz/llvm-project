# This cache file configures a build with the FeMe project enabled (see
# feme/docs/Design.md), including its `mlir` dependency (added implicitly by
# llvm/CMakeLists.txt when "feme" is in LLVM_ENABLE_PROJECTS) and the tests
# needed for the `check-feme` target.
#
# Usage (from the monorepo root):
#   cmake -G Ninja -C feme/cmake/caches/feme.cmake -B build llvm
#   ninja -C build check-feme

set(LLVM_ENABLE_PROJECTS "feme" CACHE STRING "")

# FeMe does not yet retarget to native ISA (see the Roadmap in
# feme/docs/Design.md), so only the native target is needed for its own
# tests; downstream builds enabling additional projects can override this.
set(LLVM_TARGETS_TO_BUILD "Native" CACHE STRING "")

set(LLVM_INCLUDE_TESTS ON CACHE BOOL "")
set(LLVM_BUILD_TESTS ON CACHE BOOL "")
set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "")
