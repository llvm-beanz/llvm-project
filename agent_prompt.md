---
model: claude-opus-5
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

I've added a bunch of test collateral under feme/test/Translate/DXBC. There are
two types of files I've added, dxasm files and ".ref" files that are LLVM IR.
These files come from the test data for DXC's dxilconv tool.

I'd like you to first ensure that the dxbc assembler can handle all the dxasm
files.

Then I'd like you to use these files as test data to implement the DXBC->DXIL
translation. As you work through these issues translate the ".ref" files into
FileCheck check lines in the dxasm files. The translation doesn't need to
identically match DXC's dxilconv's translation, but they should semantically
match, and if there are known differences I'd like you to note them in your
thoughts.
