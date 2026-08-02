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

The tests in the SPIRVImporterTest.cpp file that cover actual valid modules
(buildMinimalSPIRVBinary), should be lit tests running against the command-line
testing tools with LIT. Please rewrite them as such.

The same is true for the DXILImporterTest.cpp tests that depend on valid DXIL IR
(buildMinimalBitcode), but it is even more significant for the DXIL case because
DXIL is not LLVM IR, so you cannot test it by using modern LLVM to parse textual
IR.
