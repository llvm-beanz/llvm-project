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

Some of the tests in feme/test/Translate/DXBC still have ".ref" files. My
understanding is that those were left behind because the dxasm didn't fully
capture the container metadata required to generate correct transformations to
DXIL.

Please work through those test cases, updating LLVM's objectyaml tooling as
necessary so that these tests can be rewritten in the style of
feme/test/Tools/dxbc-as/full-container.test.
