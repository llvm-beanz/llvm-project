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

1. **(~30 min)** `L378(a)` (`max_sampler_lod_bias.shader_bias` wrong-pixel bug)
   — the longest-standing deferred item across multiple sessions now. Use
   `--deqp-log-images=enable` + golden-image diff. `shader_bias_compute` (same
   LOD-bias logic, compute pipeline) passes, so this is likely
   graphics-fragment-path-specific — start there.
2. **(~30 min)** The 4 `b10g11r11_ufloat_pack32` dual-source-blend
   pixel-mismatch Fails from the `L409` session — same
   `--deqp-log-images=enable` + golden-image diff technique.
3. **(~30 min)** The 37 `part1`/12 `copy_and_blit` ASTC-blit-limit Fails — still
   a not-yet-filed, low-priority roadmap entry candidate; file it if a session
   has spare time and nothing higher-priority to do.
4. **Stability triage (46 pipeline SIGSEGV + 328 renderpasses DeviceLost from
   the original full-run baseline) — still not started as its own
   investigation**, though `L409`/`L410`'s eight fixes may well have already
   resolved some or all of this count as a side effect (both failure classes are
   exactly the shape the use-after-free race produces). Recommend: re-run a
   large sample of `part2`'s full caselist and compare the `DeviceLost` rate
   against the pre-`L409` 22.74% baseline before investing in further dedicated
   triage — may already be closed.
5. Drop from future handoffs — fully resolved, stop citing: `L410` in its
   entirety (all 8 types:
   `Buffer`/`Image`/`ImageView`/`DescriptorSet`/`BufferView`/`Sampler`/`Event`/`QueryPool`,
   all CTS-verified), `L409`, `part1`'s crash mystery, `L407`, the
   "resource-table exhaustion" hypothesis, `L375`,
   `LLVM_EXTERNAL_PROJECTS=OffloadTest` (`L380`), roadmap strikethrough cleanup
   pass, `L402`/`L406`, `pipeline_library.stencil` verification, `part1`'s
   `use_after_copy.a8b8g8r8_srgb_pack32...` case.
