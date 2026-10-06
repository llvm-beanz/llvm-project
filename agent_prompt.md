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

1. **~1-2 hours**: `L387` — confirm/refute the per-subpass `ColorAttachments`
   format-resolution hypothesis via a debug print in `validateStageInterfaces`,
   using
   `dEQP-VK.renderpasses.dynamic_rendering.primary_cmd_buff.suballocation.formats.r16_uint.input.clear.store.draw`
   as the reproducer. If confirmed, likely fixes all 152 cases in this
   sub-family at once (same shape as `L379`/`L381`/`L382`'s own
   single-root-cause wins).
2. **~2-4 hours**: `L386` — isolate
   `dEQP-VK.pipeline.fast_linked_library.multisample.misc.input_attachments.render_to.r8g8b8a8_unorm_r16g16b16a16_sfloat_r16g16b16a16_sint_d16_unorm.2x.ds_resolve_sample_zero.sub_framebuffer`,
   dump canonicalized IR before/after `CanonicalizeStage.cpp` to find what
   `spirv_var_25` is and why it's unresolved. Fixes the remaining 24 of
   `L379(c)`'s original 72 cases.
3. **~1 day, needs a dedicated design session**: `L375` (`DiamondFlattener`
   stale-postdom-tree assertion, `switch_continue_{geom,tessc,tesse}` +
   `graphicsfuzz.nested-for-loops-switch-fallthrough`). Still fully scoped in
   the Roadmap, still not attempted.
4. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache-persistence item: already
   resolved/documented, no action needed.
