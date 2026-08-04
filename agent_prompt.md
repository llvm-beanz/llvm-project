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

You noted that some of the dxilconv tests (and surely other DXBC test cases)
depend on data from other parts of the DXContainer file format.

Can you use the ObjectYAML and yaml2obj tooling from LLVM to construct those
DXContainer components and merge the generated DXBC into a whole container using
llvm-objcopy?

This would allow our tests to more comprehensively cover the use cases.

You could also use LLVM's split-file tool to group the YAML and dxasm into a
single file to make tests self-contained.
