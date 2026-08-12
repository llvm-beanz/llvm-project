# This cache file configures a build with the FeMe project enabled (see
# feme/docs/Design.md), including its `mlir` dependency (added implicitly by
# llvm/CMakeLists.txt when "feme" is in LLVM_ENABLE_PROJECTS) and the tests
# needed for the `check-feme` target.
#
# Usage (from the monorepo root):
#   cmake -G Ninja -C feme/cmake/caches/feme.cmake -B build llvm
#   ninja -C build check-feme

set(LLVM_ENABLE_PROJECTS "feme;mlir" CACHE STRING "")

# FeMe's own tests need the host's native target (for FeMe's general-purpose
# retargeting -- see Roadmap in feme/docs/Design.md), SPIRV, which
# LLVM's own in-tree SPIRV backend provides and which the SPIR-V retargeting
# "null pipeline" tests need (feme::TargetMachineBackend retargeting SPIR-V
# back to itself via LLVM's SPIRV target -- see the deviation note under
# "Retargeting to Native ISA" in feme/docs/Design.md), and AMDGPU, which the
# raised-LLVM-IR -> AMDGPU lowering tests need (see the "Raised LLVM IR ->
# AMDGPU" section of feme/docs/Design.md); downstream builds enabling
# additional projects can override this to add more targets.
set(LLVM_TARGETS_TO_BUILD "Native;SPIRV;AMDGPU" CACHE STRING "")

# Include the DirectX target for DXIL code generation.
set(LLVM_EXPERIMENTAL_TARGETS_TO_BUILD "DirectX" CACHE STRING "")

set(LLVM_ENABLE_ASSERTIONS ON CACHE BOOL "")

# Force PCH off unconditionally, rather than relying on
# HandleLLVMOptions.cmake's own default (which only disables PCH for a
# handful of compiler/launcher combinations, e.g. sccache -- see
# CMAKE_CXX_COMPILER_LAUNCHER matching there). Left on its own default, a
# ccache-launched Clang >= 18 build enables PCH, and the shared LLVMCore PCH
# then transitively supplies headers (e.g. llvm/IR/Constants.h) that a
# translation unit only relying on a forward declaration (e.g. from
# llvm/IR/Instructions.h) needs but never explicitly included. That masks
# genuine missing-#include bugs in a PCH-enabled build while still failing to
# build for anyone whose toolchain disables PCH (including every combination
# HandleLLVMOptions.cmake itself disables it for, like sccache). Forcing it
# off here makes every configuration -- and, importantly, this project's own
# validation builds -- exercise the same "headers must compile standalone"
# rule feme/.instructions.md already requires.
set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON CACHE BOOL "")
