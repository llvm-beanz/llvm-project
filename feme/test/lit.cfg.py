# -*- Python -*-

import os

import lit.formats
from lit.llvm import llvm_config

# Configuration file for the 'lit' test runner.

config.name = "FEME"
config.test_format = lit.formats.ShTest()

# suffixes: A list of file extensions to treat as test files.
config.suffixes = [".test", ".mlir", ".ll"]

# test_source_root: The root path where tests are located.
config.test_source_root = os.path.dirname(__file__)

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(config.feme_obj_root, "test")

config.excludes = ["Inputs", "CMakeLists.txt", "README.txt", "LICENSE.txt"]

# Targets: expose a `<arch>-registered-target` feature per LLVM target
# configured into this build, mirroring llvm/test/lit.cfg.py, so tests
# needing a specific codegen target (e.g. the SPIR-V "null pipeline", see
# feme/docs/Design.md) can `REQUIRES:` it rather than failing on builds that
# don't configure that target.
for arch in config.targets_to_build.split():
    config.available_features.add(arch.lower() + "-registered-target")

llvm_config.use_default_substitutions()

tool_dirs = [config.feme_tools_dir, config.llvm_tools_dir]
tools = [
    "feme",
    "feme-opt",
    "feme-translate",
    # DXIL (see feme/docs/Design.md's DXIL section) is plain LLVM bitcode,
    # optionally wrapped in a DXContainer; tests build fixtures at test time
    # with these existing, upstream LLVM tools instead of checking in
    # binary fixtures (see "Avoiding binary test fixtures" in
    # feme/docs/Design.md).
    "llc",
    "llvm-as",
]

llvm_config.add_tool_substitutions(tools, tool_dirs)
