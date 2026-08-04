# -*- Python -*-

import os

import lit.formats
from lit.llvm import llvm_config

# Configuration file for the 'lit' test runner.

config.name = "FEME"
config.test_format = lit.formats.ShTest()

# suffixes: A list of file extensions to treat as test files.
config.suffixes = [".test", ".mlir", ".ll", ".dxasm"]

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
    # See the "dxbc-as" section of feme/docs/Design.md: a standalone DXBC
    # assembler with no MLIR/feme::Context dependency, used to build
    # human-readable DXBC test fixtures at test time (see "Avoiding binary
    # test fixtures" below).
    "dxbc-as",
    # DXIL (see feme/docs/Design.md's DXIL section) is plain LLVM bitcode,
    # optionally wrapped in a DXContainer; tests build fixtures at test time
    # with these existing, upstream LLVM tools instead of checking in
    # binary fixtures (see "Avoiding binary test fixtures" in
    # feme/docs/Design.md).
    "llc",
    "llvm-as",
    # Used by dxil-raise-ops-roundtrip.ll to produce a fixture with LLVM's
    # own `-dxil-op-lower` pass, so that test validates feme's op-raising
    # pass against real DXIL-lowering output rather than only hand-written
    # `dx.op.*` calls.
    "opt",
    # Used to assemble complete legacy DXBC containers at test time: yaml2obj
    # builds the parts LLVM's ObjectYAML models structurally or that a test
    # authors verbatim (e.g. ISGN/OSGN), llvm-objcopy merges in the raw
    # bytecode part dxbc-as produces, obj2yaml inspects the result, and
    # split-file keeps the YAML and the .dxasm it is paired with in one
    # self-contained test file (see feme/docs/Design.md's "dxbc-as" section).
    "split-file",
    "yaml2obj",
    "obj2yaml",
    "llvm-objcopy",
]

llvm_config.add_tool_substitutions(tools, tool_dirs)
