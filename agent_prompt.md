---
model: claude-sonnet-5
resume: 1ebfe39e-5f48-4c14-810b-08a0301888a7
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

Please continue working on the FeMe Vulkan ICD. The previous session's suggested
next steps are:

1. **(a few hours, new this session)** The 6
   `samplemask_{early,no_early}_fragment_tests_depth_samples_{2,4,8}` failures
   above -- a distinct MSAA depth-resolve bug, not an `early_fragment_tests`
   issue (the `no_early` variant fails too). Start by instrumenting/dumping the
   per-sample depth values at a few pixels right after the draw, before resolve,
   to see whether the bug is in the per-sample test/write itself or in the
   resolve step afterward.
2. **(a few hours)** Continue handoff item 1's remaining untriaged clusters:
   `device_group` (7 cases), `geometry.input.triangle_strip_adjacency` (6
   cases), `memory_model.*` races (~24 cases). None investigated yet.
3. **(systematic grep, ~1 hour, carried over)** Audit other Vulkan-layer objects
   for the "stale synchronous-execution assumption" bug class `QueryPool` had
   (fixed as `L354`). `Fence`/`Semaphore`/`QueryPool` confirmed fine; nothing
   else has been checked.
4. **(dedicated session, carried over many sessions, unchanged)** `L344` item 2
   / `L335` -- N-barrier generalization, 1 case (`shader_input_output.barrier`).
   Fully scoped in `FeMeGraphicsDesign.md`; still needs the actual multi-file
   implementation session.
5. **(lowest priority, many sessions carried over, unchanged)** `L265` -- ASTC
   alpha-decode tie-break, 12 cases. Next angle (still unattempted): compare
   decoded 4-texel neighborhoods pixel-by-pixel between `astc_5x5`/`astc_8x8`
   for a structural property correlating with tie direction.
6. **(a few hours, overdue, carried over)** Resume/complete the
   broader-than-tessellation CTS sweep -- still only covered 240,182 cases from
   several sessions back (died mid-run). `binding_model` was explicitly skipped
   for being too large; worth relaunching in the background early next session.
