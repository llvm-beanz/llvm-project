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

Can you continue V6 from the roadmap document?

> Graphics queue and basic rendering: graphics stage compilation, `VkRenderPass`
> and dynamic rendering, graphics pipeline state, draws, and
> `VK_QUEUE_GRAPHICS_BIT`

The previous agent left the notes:

> I did not attempt the graphics pipeline cache entry this pass, after
> looking at it seriously enough to explain why: unlike compute's cache key
> (computed from raw SPIR-V words and the pipeline layout *before*
> compiling, so a hit skips compilation entirely), a graphics pipeline's
> normalized state includes vertex attributes and attachment formats that
> this ICD currently only finalizes after `compileGraphicsPipeline` has
> already run the (expensive) stage compilation. A cache key computed after
> compiling buys only artifact-sharing across pipeline handles, not a
> skipped recompile, unless the fixed-function-state translation is hoisted
> ahead of stage compilation first -- a real refactor of
> `compileGraphicsPipeline`'s control flow, not the glue code a cache key
> function alone would be. Rather than land a cache that reads as complete
> but provides none of the compute cache's actual benefit, I left it open
> and said why here instead of quietly declaring it done. Blits still do not
> convert formats, mirror, or handle multisample sources; per-instance
> vertex input rate and primitive restart remain unimplemented; and
> secondary command buffers inside a render pass are still V7's own bullet.
