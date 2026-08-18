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

Can you continue working on outstanding V4 items from the roadmap document?

> Typed buffers, `VkFormat` mapping, texel buffers, broader
> subgroup/atomic/robustness coverage, persistent pipeline cache with a blob
> fuzzer, first CTS runs over the advertised subset

The previous agent left the notes:

> - SPIR-V atomic buffer/image access (`spirv.Atomic*`): no dialect
>   conversion pattern exists at all; needs one plus a new
>   `SPIRVResourceLoweringPass` access shape. Documented in
>   FeMeVulkanDesign.md's V4 Status note rather than left silently
>   unaddressed.
> - Texel-buffer format coverage beyond `R32G32B32A32_SFLOAT`/
>   `R8G8B8A8_UNORM`: needs the CPU runtime helper library to grow more
>   `ResourceCallKind`-mangled `<N x T>` variants.
> - Relocatable object code in the persistent pipeline cache blob: depends
>   on a `CompiledStage`/`CompiledKernel` API this milestone doesn't add
>   (the design doc's own anticipated dependency).
> - An actual Vulkan CTS run and its result: no `deqp-vk` build available
>   in this environment; only the filtering/harness infrastructure landed.
