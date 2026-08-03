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

Now that we have dxbc-as, can you integrate the dxsa dialect from the
wip/dxsa-mlir branch of the access softek fork of LLVM
(https://github.com/access-softek/llvm-project). Please integrate the dialect
under the feme project rather than as a part of the mlir project, and migrate
the tests to use the dxbc-as tool wherever possible to avoid binary and
hex-encoded files as test collateral.
