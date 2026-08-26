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

Also please run the Vulkan CTS from the checkout under /home/dev/dev/VK-GL-CTS/
after each change and update the VulkanCTSReport.md. Please keep the
Vulkan14FeatureInventory and VulkanExtensionInventory up to date with each
change as well.

If the request is to complete a roadmap stage, if you complete it please strike
it through on the roadmap document, if you do not, please add entries to the
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on milestone H2g?

> **A widespread rendering-correctness gap spanning most of
> `dEQP-VK.multiview`'s subgroups** (334 of H2d's own measured 421 failures, now
> 358 once H2e's own 24 `input_instance` cases joined this bucket instead of
> failing earlier at pipeline creation -- `clear_attachments`, `index`,
> `secondary_cmd_buffer`, `readback_{implicit,explicit}_clear`,
> `multisample{,_resolve}`, `masks`, `instanced`, `input_attachments`,
> `draw_indirect{,_indexed}`, `draw_indexed`, `stencil`, `depth`,
> `input_instance`, each in both `renderpass2`/`dynamic_rendering` variants):
> plain image-comparison failures, the first real check of these tests' rendered
> output at all (every one previously hit H2a's SIMDize crash before ever
> reaching image comparison). Spot-checking `instanced.no_queries.15` rules out
> H2e's own read-back gap (a plain, unconditional multi-branch
> `gl_Position`/`out_color` write) as the shared cause -- root cause otherwise
> undetermined, needs its own triage pass before assuming a single fix closes
> all 358
