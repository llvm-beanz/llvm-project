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

1. **(~2-4 hours)** `Image`/`ImageView` next for `L410` — likely the next
   highest-value target (bound via descriptor writes and render-pass
   attachments). Same 3-commit pattern. Will need a wider `Sync.cpp` audit since
   images are referenced indirectly through descriptor sets, not just direct
   `RecordedCommand` fields — check whether `DescriptorSet`'s own bound
   image/buffer views need to be walked too, or whether `Image`/`ImageView`'s
   own fix can be scoped independently first.
2. **(~2-4 hours each)** Then `DescriptorSet`, `Event`, `QueryPool` — one per
   session, same pattern.
3. **(~30 min)** Still outstanding, lower priority: `L378(a)`
   (`max_sampler_lod_bias.shader_bias` pixel bug) and the 4
   `b10g11r11_ufloat_pack32` dual-source-blend Fails from the `L409` session —
   both pixel-correctness issues, unrelated to the UAF work, use
   `--deqp-log-images=enable` + golden-image diff for both.
4. **(~30 min)** The 37 new `part1` Fails (api.info/copy_and_blit/binding_model)
   are a new, low-priority, not-yet-filed finding — worth a roadmap entry if a
   future session has spare time, but not urgent (pre-existing
   feature-consistency/ASTC-blit-limit issues, not regressions).
5. Drop from future handoffs — fully resolved, stop citing: `part1`'s crash
   mystery (closed this session, see above), `L409` (closed,
   Pipeline/RenderPass/Framebuffer fixed and verified), `L407`, the
   "resource-table exhaustion" hypothesis, `L375`,
   `LLVM_EXTERNAL_PROJECTS=OffloadTest` (`L380`), roadmap strikethrough cleanup
   pass, `L402`/`L406`, `pipeline_library.stencil` verification, `part1`'s
   `use_after_copy.a8b8g8r8_srgb_pack32...` case.
