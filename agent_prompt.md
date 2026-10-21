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

1. **(~30-60 min, new finding, best next target)** The JIT "Symbols not found:
   spirv_var_N" cluster — 3 cases (`barrier.in_function`,
   `barrier.after_divergent_if`, `indexing.output.component_tessc`) share this
   exact signature. Find what's common about these shaders' symbol resolution
   (likely a resource/global that isn't getting registered/materialized in the
   JIT session for some specific CFG or storage-class shape) before guessing at
   a fix.
2. **(~1-2 hrs)** `ENV5(c)(4)`: the 6 deeper `float16.*_tessc` cases
   (`struct16arr3`/`v3f16arr5`/`v4f16arr3` variants). Same
   `foldRedundantFlowBlock` bug family as this session's fix, but the two
   aliasing predecessors are no longer immediate fold targets — need either a
   transitive-reconvergence check or whole-fixed-point-loop slot-write tracking.
   Get a fresh standalone `.ll` reduction first (same technique as this
   session).
3. **(~30-60 min)** `barrier.in_if`/`in_switch`/`in_loop` (3 cases) — these are
   an explicit "not yet supported" milestone limitation (one barrier per
   tess-control entry point), not a bug; check `Roadmap.md` for whether lifting
   this limitation is already a scheduled milestone or needs a new entry.
4. **(~30-60 min)** `module.same_module*` (4 cases) — no creation-time error;
   needs render-output comparison to find what's actually wrong, a different
   diagnostic approach than every other cluster this session.
5. **(new, large, never triaged)** `ENV6(a)`: `api.copy_and_blit` (210 cases, by
   far the single largest cluster in the now-final 666-case corpus). Worth a
   first pass to check whether they share one root cause — concentrated in
   `depth_stencil_msaa_copy`/`resolve_image` with `d32_sfloat`-family formats
   recurring.
6. **(low priority, unchanged for many sessions)** Upstream the 3
   `offload-test-suite` patches if push access ever becomes available.
