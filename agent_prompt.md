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

Let's iterate on the FemeCPUDesign.md design.

Wave size must be a power of two in the range 4->128. If unspecified by the
shader or the user default it to max(4, host vector width / 32). If unspecified
by the user, but the shader does specify it, use the shader specified value.

If the shader specifies a value and the user specifies a different value, error.

Let's make the host CPU emulation only work with "bindless" shaders based on
DXIL SM 6.6+ and the SPIRV descriptor heaps extension.

Yes, all memory accesses through descriptors should be bounds-checked returning
zero for OOB reads and ignoring OOB writes.

The JITEngine should own the dispatch management.

What would change about the design if we wanted to account for graphics?
Initially compute-only is fine, but longer term graphics might be nice.

We must handle DXIL inputs.

The design should operate on llvm::Modules to share more code between SPIRV and
DXIL inputs.
