---
model: claude-sonnet-5
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

When you build and test ensure that you are using object file caching, and
building with assertions enabled.

When you deviate from the design document please update the design document.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

One of our output formats for translation should be "llvm", which is updating
the input file to normalized LLVM IR that could be fed into llc/opt and other
LLVM tools or backends.

For DXIL, this will require some translation and fixup passes that will replace
dx.op function calls with DirectX backend or LLVM intrinsics, and transform IR
metadata from the DXIL format to the formats used in the LLVMFrontendHLSL
library to describe IR metadata.

For SPIRV, we need to do a similar translation of SPIRV instructions into LLVM
IR and SPIRV backend intrinsics, and produce correct LLVMFrontendHLSL metadata
(as appropriate).

You can use the test cases in the offlooad-test-suite as test collateral for
this next phase by compiling the tests to DXIL or SPIRV and using that as inputs
to the testing tools to flesh out all the transformations required to convert
DXIL -> LLVM IR, and SPIRV -> LLVM IR.
