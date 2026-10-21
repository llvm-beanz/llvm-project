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

`ENV6(b)(3)`'s remaining 15 cases, in priority order:
1. `spec_constant.graphics.{geometry,tess_control}.expression.array_size_pass_to_function`
   (6 cases) and
   `max_varyings.test_{geometry_io_between_geometry_fragment,tess_eval_io_between_tess_eval_fragment}`
   (6 cases) -- both confirmed genuine rendering-correctness bugs (`Fail (Values
   did not match)`/`Fail (Rendered image is not correct...)`), **best next
   target**. No `FEME_VULKAN_LOG_CREATION_ERRORS` diagnostic exists for either
   (confirmed this session for `spec_constant`), so this needs
   `FEME_DUMP_IR_PRELINEARIZE`/`FEME_DUMP_IR_PRENORM`-style IR investigation,
   not the simpler creation-error pattern every `ENV6(b)` fix so far has used.
   Sample one case from each sub-cluster first to check for a shared root cause
   before splitting further.
2. `cache.misc_tests.invalid_size_test` (3 cases) -- suspected CTS-side harness
   assertion per the existing `H29f` roadmap note, not independently
   re-confirmed this session. Lowest priority of the three.
3. `ENV6(b)(1)`/`ENV6(b)(2)`: `emulate_msrtss` (8
   cases)/`multisample_interpolation` (12 cases) -- confirmed genuine
   rendering-correctness bugs from earlier sessions, not yet root-caused,
   unchanged.
4. `ENV6(c)`: `graphicsfuzz` (106 cases) -- unchanged from prior sessions,
   likely a long tail of distinct small bugs.
5. `ENV6(d)`: `memory_model`/`tessellation`/`glsl`/`image`/`mesh_shader` (132
   cases combined) -- unchanged from prior sessions.
6. `ENV5(c)(7)(b)`: hull-stage lockstep barrier model (3 cases) -- scoped, not
   started, lowest priority.
7. (low priority, unchanged for many sessions) Upstream the 3
   `offload-test-suite` patches if push access ever becomes available.
