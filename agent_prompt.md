---
model: claude-sonnet-5
resume: db99d377-bdef-4dd8-a2b5-787bcf66bc23
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file, and the environment-wide
agent skills at /home/dev/.agents/skills.

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

**Before doing anything else**: `vulkaninfo --summary | grep deviceName` and
confirm `FeMe CPU Vulkan Device`. Every session from now on, every time, not
just once at the start.

**When you find an issue outside FeMe**: Create an isolated reproducer, fix it,
and apply the fix in a commit that only touches files from outside the FeMe
subdirectory. Ensure that fixes to other LLVM sub-projects are self-contained
and tested.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~2-4 hours)** `DescriptorSet` next for `L410` — same 3-commit pattern
   (mixin → destroy guard → retain/release). This is also the one that finally
   closes the descriptor-indirect image/buffer gap noted above, since
   `DescriptorSet`'s own retain sweep is the natural place to walk its bound
   image/buffer views.
2. **(~2-4 hours each)** Then `Event`, `QueryPool` — one per session, same
   pattern. `L410` is then fully closed.
3. **(~30 min)** Still outstanding, unrelated pixel-correctness bugs, lower
   priority: `L378(a)` (`max_sampler_lod_bias.shader_bias`) and the 4
   `b10g11r11_ufloat_pack32` dual-source-blend Fails from the `L409` session.
   Use `--deqp-log-images=enable` + golden-image diff for both.
4. **(~30 min)** The 37 `part1`/12 `copy_and_blit` ASTC-blit-limit Fails — still
   a not-yet-filed, low-priority roadmap entry candidate if a future session has
   spare time.
5. Drop from future handoffs — fully resolved, stop citing: `L409` (closed,
   verified), `Buffer`'s own `L410` fix (closed, verified), `part1`'s crash
   mystery, `L407`, the "resource-table exhaustion" hypothesis, `L375`,
   `LLVM_EXTERNAL_PROJECTS=OffloadTest` (`L380`), roadmap strikethrough cleanup
   pass, `L402`/`L406`, `pipeline_library.stencil` verification, `part1`'s
   `use_after_copy.a8b8g8r8_srgb_pack32...` case.
