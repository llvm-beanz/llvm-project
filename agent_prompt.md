---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
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
roadmap document to break down the remaining work for that milestone.

During the H6 milestone breakdowns things have gone a little crazy with nesting
letters in strange ways. Please avoid nesting milestones more than one lowercase
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

# Request

Can you work on H98a or other blocking work to make progress on the H-series
milestones?

> **`image.host_image_copy.draw_*`'s 66-case pixel-comparison failure family**
> (newly exposed by H98's own closing re-run once the systemic crash it
> previously masked was fixed): every failure is in the `draw_<format>`
> subfamily (e.g. `draw_r32g32_sfloat_r32g32_sfloat`, `draw_r8_unorm_r8_unorm`)
> specifically for `transfer_src_transfer_dst` usage combined with a
> `general`/`optimal` layout pair at a small (16x16 or 53x61) extent, across
> every one of that subfamily's
> `barrier_transition_host_copy`/`host_transition`/`host_transition_host_copy` x
> `image_to_memory`/`memcpy`/`memory_to_image` action combinations -- a narrow,
> specific shape (renderable-format host copies around an actual draw call), not
> a general regression of the `dispatch_*`/`simple.*` host-copy paths H98's own
> fix already made pass. Not yet triaged -- needs a single-case repro (e.g.
> `draw_r8_unorm_r8_unorm.host_transition.memcpy.transfer_src_transfer_dst.general.optimal.0_1_0.16x16`)
> and a qpa-image/channel-level pixel reduction (mirroring H88/H93's own
> technique) to determine whether the drawn content itself, the host-copy
> readback, or the transfer-src/transfer-dst layout transition around the draw
> is the actual source of disagreement
