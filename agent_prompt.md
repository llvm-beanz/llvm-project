---
model: claude-sonnet-5
resume: 1b814ab3-f423-4724-ae98-f676112844de
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

**When you find an issue with environment or process**: Codify it in Feme's
.instructions.md file so future sessions don't lose it.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

**Always**: End each session with a commit of the agent thoughts even if
mid-investigation to avoid losing documentation.

# Request

Please continue working on the FeMe Vulkan ICD. The previous session suggested
the next steps:

`ENV6`'s remaining untriaged clusters, in priority order:
1. `ENV6(b)(1)`: `multisample.misc.emulate_msrtss` (8 cases) -- root-caused in
   an earlier session (`VK_ATTACHMENT_UNUSED` subpass-input descriptor fallback
   missing), not yet fixed. Scoped in `Roadmap.md`: (a) thread the
   descriptor-set image handle through `feme.stage.subpass.load` as a new
   operand, (b) add a CPU-runtime fallback read when the live-attachment heap
   slot for a requested index is unmapped, (c) new lowering-phase and
   runtime-phase unit tests, (d) re-verify the whole existing subpass-input CTS
   corpus for regressions. **Best next target** of the two scoped-but-unstarted
   `ENV6(b)` gaps -- slightly smaller (8 vs 9 cases) and already has a fuller
   implementation sketch from last session.
2. `ENV6(b)(2)(a)`: `centroid_qualifier_inside_primitive` (9 cases) --
   root-caused this session
   (plain-pixel-center-instead-of-coverage-weighted-centroid), not yet fixed.
   Needs a per-quad coverage mask threaded into
   `FemeFragmentPrimitive`/`FragmentWrapper.cpp`'s `InterpolateAtCentroid` case,
   a runtime-ABI change with new lowering-phase and runtime-phase tests, plus
   re-verification of the rest of the
   centroid-qualifier/`multisample_interpolation` corpus. Multi-session effort;
   do not rush it into one sitting.
3. `ENV6(c)`: `graphicsfuzz` (106 cases) -- unchanged from prior sessions,
   likely a long tail of distinct small bugs.
4. `ENV6(d)`: `memory_model`/`tessellation`/`glsl`/`image`/`mesh_shader` (132
   cases combined) -- unchanged from prior sessions;
   `memory_model.{write_after_read,message_passing}` (52) and
   `tessellation.invariance` (26) worth checking for a shared cause first.
5. `ENV5(c)(7)(b)`: hull-stage lockstep barrier model (3 cases) -- scoped, not
   started, lowest priority.
6. (low priority, unchanged for many sessions) Upstream the 3
   `offload-test-suite` patches if push access ever becomes available.
