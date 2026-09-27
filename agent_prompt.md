---
model: claude-sonnet-5
resume: ed0156d8-7270-4d71-9053-e6cff6d7f6b5
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

# Request

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(20-30 min, start here)** Build a numeric diagnostic: dump
   `vert_f32d3_flat_4`'s interpolated value (`.x` component is enough) and its
   independently-computed min/max bounds into the output color, scaled/clamped
   for the 8-bit UNORM format, across the full 8x8 render. Look for "wildly
   unrelated value" (indexing/addressing bug in FeMe's SPIR-V-to-LLVM lowering
   of per-invocation output/input array access chains) vs. "just outside the
   boundary by a small amount" (precision bug in domain-point/barycentric-weight
   generation).
2. **(unknown, depends on #1)** Once the failure class is known, read
   `HullWrapper.cpp`/`DomainWrapper.cpp` (and whatever shared
   access-chain-normalization code they funnel through) for an indexing bug
   specific to per-vertex-array location counts around 13-29, or look at FeMe's
   software tessellator's own tess-coordinate generation if #1 points to
   precision instead.
3. **(unknown)** L201(b)/(c) (27+25 cases) -- still just "reduced to distinct
   symptoms," never started, carried over many sessions now.
4. A full/broad CTS re-run is overdue (last full run: 2026-09-26). Worth doing
   once L216 (or a solid chunk of it) lands, for one consolidated before/after
   instead of piecemeal reruns.
5. `check-hlsl-feme-vk` parallel-worker flakiness was tentatively attributed to
   L211 last session (unproven, circumstantial) -- if it ever reproduces again,
   it needs its own from-scratch root-causing.
6. No git stashes this session (confirmed empty both repos).
