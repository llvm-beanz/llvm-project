---
model: claude-opus-5
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

Can you address the documentation debt from the Roadmap document:

## 3.4 Documentation debt

Three documentation items are prerequisites for the steps above rather than
follow-ups, and each is small:

> - **FeMeVulkanDesign.md has no V6–V8.** The graphics design lists what they
>   unblock but explicitly does not own their Vulkan-side content (graphics
>   queue family, `VkRenderPass`/dynamic rendering, graphics pipeline state,
>   WSI). Required before R32's work has a Vulkan consumer to aim at.
> - **Design.md's tool list and `docs/CommandGuide/` need `feme-render`**
>   (R31), which is also where the textual scene and image fixture formats
>   should be specified.
> - **DXIL texture/sampler handle kinds still need a decision recorded in
>   Design.md's DXIL section** before R30 implements them — §1.3 has flagged
>   this as blocking since before the graphics design existed.
