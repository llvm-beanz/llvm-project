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

A previous prompt left the SPIRV to LLVM functionality incomplete because of the
need to generate target-specific intrinsics.

MLIR supports setting the target triple and data layout as module attributes
which can then be passed down through the LLVM IR dialect. This will allow the
LLVM IR dialect to reference direct target intrinsics (prefixing them with
`llvm.`). Can you use this to enable the SPIRV to LLVM path to generate
full-featured LLVM IR that can lower into the SPIRV backend?
