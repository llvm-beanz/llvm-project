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

There is a set of remaining .ref files under feme/test/Translate/DXBC, which
represent partially migrated tests from DXC's dxilconv. These tests are
partially translated because of some missing functionality in LLVM or FeMe
prevents them from fully working.

Can you please work through the remaining .ref files and address all outstanding
issues and migrate the tests?
