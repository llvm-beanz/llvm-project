import os

import lit.formats
from lit.llvm import llvm_config

config.name = "DXIL tools"
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = [".ll", ".td"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.dxil_test_exec_root

llvm_config.use_default_substitutions()
llvm_config.add_tool_substitutions(
    ["dxil-as", "dxil-dis", "dxil-tblgen", "llvm-bcanalyzer", "FileCheck"],
    [config.llvm_tools_dir],
)
config.substitutions.append(("%dxil_include", config.dxil_include_dir))
