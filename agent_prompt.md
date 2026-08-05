---
model: gpt-5.6-sol
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
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

There are still a number of *.ref files in the feme/test/Translate/DXBC test
folder. Those files correspond to un-migrated tests from DXC's dxilconv, and
have a *.dxasm file next to them that is the test input.

Please migrate the remaining *.ref files into FileCheck `CHECK` lines in the
source tests, and update the translation and testing infrastructure as necessary
to enable these tests.
