# This cache file configures a build with the FeMe project enabled (see
# feme/docs/Design.md), including its `mlir` dependency (added implicitly by
# llvm/CMakeLists.txt when "feme" is in LLVM_ENABLE_PROJECTS) and the tests
# needed for the `check-feme` target.
#
# Usage (from the monorepo root):
#   cmake -G Ninja -C feme/cmake/caches/feme.cmake -B build llvm
#   ninja -C build check-feme

set(LLVM_ENABLE_PROJECTS "feme" CACHE STRING "")

# FeMe's own tests need the host's native target (for FeMe's general-purpose
# retargeting -- see Roadmap in feme/docs/Design.md) plus SPIRV, which
# LLVM's own in-tree SPIRV backend provides and which the SPIR-V retargeting
# "null pipeline" tests need (feme::TargetMachineBackend retargeting SPIR-V
# back to itself via LLVM's SPIRV target -- see the deviation note under
# "Retargeting to Native ISA" in feme/docs/Design.md); downstream builds
# enabling additional projects can override this to add more targets.
set(LLVM_TARGETS_TO_BUILD "Native;SPIRV" CACHE STRING "")

set(LLVM_INCLUDE_TESTS ON CACHE BOOL "")
set(LLVM_BUILD_TESTS ON CACHE BOOL "")
set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "")
