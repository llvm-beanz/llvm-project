# -*- Python -*-

import os

import lit.formats
from lit.llvm import llvm_config

# Configuration file for the 'lit' test runner.

config.name = "FEME"
config.test_format = lit.formats.ShTest()

# suffixes: A list of file extensions to treat as test files.
config.suffixes = [".test", ".mlir"]

# test_source_root: The root path where tests are located.
config.test_source_root = os.path.dirname(__file__)

# test_exec_root: The root path where tests should be run.
config.test_exec_root = os.path.join(config.feme_obj_root, "test")

config.excludes = ["Inputs", "CMakeLists.txt", "README.txt", "LICENSE.txt"]

llvm_config.use_default_substitutions()

tool_dirs = [config.feme_tools_dir, config.llvm_tools_dir]
tools = [
    "feme",
    "feme-opt",
    "feme-translate",
]

llvm_config.add_tool_substitutions(tools, tool_dirs)
