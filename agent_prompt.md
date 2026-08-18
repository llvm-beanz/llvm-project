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

> - SPIR-V atomic buffer/image access (`spirv.Atomic*`): still no dialect
>   conversion pattern at all in MLIR's `SPIRVToLLVM.cpp`; still needs a new
>   `feme::spirv` conversion pattern plus a `feme::cpu` canonicalization step.
>   Left untouched this session — a strictly larger change than the format
>   work above, and not one that could be split into small, independently
>   testable commits without first landing the conversion pattern itself.
> - Texel-buffer formats needing per-format channel-count padding (`R32_UINT`,
>   `R32G32_UINT`, ...) or additional packed-format scalar conversions
>   (`R8G8B8A8_SNORM`/`_UINT`/`_SINT`, `R16G16B16A16_*`, `R11G11B10_FLOAT`,
>   `R10G10B10A2_*`): each is a mechanical repeat of this session's pattern
>   once the per-format padding/conversion logic exists, but that logic itself
>   doesn't yet, so widening the whitelist to include them now would silently
>   misconvert rather than correctly handle them.
> - Relocatable object code in the persistent pipeline cache blob: still
>   depends on a `CompiledStage`/`CompiledKernel` API this milestone doesn't
>   add.
> - An actual Vulkan CTS run and its result: `deqp-vk` remains unavailable in
>   this sandboxed environment; only the filtering/harness infrastructure from
>   the previous session exists.
