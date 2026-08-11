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
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

I'd like to start a new design document, I've created an empty document
FeMeCPUDesign.md as our starting point.

I'd like to design a way to target SPIRV and DXIL programs to CPUs through LLVM
IR. This will require transforming the program IR to SIMD-ized IR, and should
support a user-provided wave size.

We'll also need some sort of resource binding model, and JIT flow for this
design. Please think through this a bit and come up with a basic proposal and
ask any questions that you need to help elaborate on this more.
