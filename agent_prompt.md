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

> The completion test says "match lavapipe for every format and state
> combination the driver reports". That did not happen: `deqp-vk` was not
> available here, and I did not stand up an off-screen lavapipe differential.
> I have said so plainly in the status note rather than letting the
> milestone's own completion criterion quietly become "the unit tests pass".
> Whoever picks this up next should treat that as V6's outstanding debt, not
> as V7 work -- the differential harness `feme-vulkan-storage-buffer-diff`
> already established for compute is the obvious model, and
> `feme-vulkan-graphics-smoke` is already the client to generalize.
>
> Also open, and smaller: no graphics pipeline cache entry (the key must
> cover the normalized pipeline description and the render-target binding,
> and a key covering less is worse than none); blits do not convert formats,
> mirror, or handle multisample sources; per-instance vertex input rate and
> primitive restart are unimplemented; and secondary command buffers recorded
> *inside* a render pass are V7's own bullet, so `VkCommandBufferInheritance
> Info` is not interpreted yet.
