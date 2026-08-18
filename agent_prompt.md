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

Authentication failed for an unspecified reason, can you retry the request below.

Can you flesh out the R30 and V5 gaps from the roadmap document?

The previous agent left the comment:

> - Real shader-side image/sampler consumption (materializing an `ImageHeap`/
>   `SamplerHeap` from a Vulkan descriptor set for a dispatch): blocked on
>   R30's remaining compiler-side work, as scoped above. This is the
>   milestone's one real gap versus its own bullet list ("storage and sampled
>   images" exist as objects and descriptor bindings, but nothing can read or
>   write one from a compiled shader yet).
> - Multisample images (`VK_SAMPLE_COUNT_1_BIT` only), format-converting
>   `vkCmdCopyImage` (same format required), and a loader-level end-to-end
>   lit test, all for the reasons given above.
> - `VK_EXT_custom_border_color`/`_border_color_swizzle`: rejected outright
>   at `vkCreateSampler`, since neither extension is advertised.
