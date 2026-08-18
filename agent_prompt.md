---
model: claude-sonnet-5
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

When you build and test ensure that you are using object file caching, and
building with assertions enabled. Also build and test the `check-feme` target
ensuring that all the target dependencies are correctly setup so that the test
dependencies will build before running the tests.

When you deviate from the design document please update the design document.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement V2 from the roadmap document?

> Storage buffers and descriptors, descriptor pools/sets/updates and dynamic
> offsets, buffer copies and barriers, lavapipe differential

The previous agent left the notes:

> - Descriptor sets/pools/updates, dynamic offsets, buffer copies, and barriers --
>   exactly what V2's own bullet list already says.
> - General `VkSpecializationInfo` application beyond group-size-relevant
>   constants (see above).
> - Parallelizing independent workgroups within one dispatch across a worker pool
>   (currently sequential; see the `JITEngine` bypass note above) -- a performance
>   follow-up, not a correctness gap.
> - `feme::spirv`/MLIR's own `BuiltIn WorkgroupSize`-on-spec-constant- composite
>   deserialization gap is unfixed upstream; it only affects group- size
>   resolution today because this milestone routes around it entirely, but any
>   future consumer that needs the same information through MLIR's structured API
>   (e.g. a hypothetical SPIR-V `dxc`/`spirv-opt` combining pass) will hit the
>   same wall.
