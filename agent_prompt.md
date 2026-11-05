---
model: claude-sonnet-5
resume: 86dca4d4-0d8b-4652-b37b-c8ab60c0ce73
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

Please begin work on the WARP roadmap.

## Suggested next steps (ranked)

1. **`WS1(b)`**: move normalized image-view materialization (format/
   swizzle/subresource-range rebasing for sampling, storage, and
   attachment views) out of `ImageOps.cpp`/`CommandBuffer.cpp` into
   `FeMeImage`, building on `WS0(a)`/`WS0(b)`/`WS1(a)`. Expect this to be
   harder than `WS1(a)` -- it touches live `Image` state, not pure
   functions -- so scope and possibly further break it down (`WS1(b)(1)`
   is not allowed by the nesting rule, so if it needs splitting, use
   separate `WS1(b)`/`WS1(d)`-style siblings instead, or keep `WS1(b)`
   itself narrowly scoped to one view kind at a time within one session).
2. **`WS1(c)`**: reusable copy/resolve kernels out of `CommandBuffer.cpp`,
   building on `WS1(b)`. Independent enough conceptually that it could
   also run before `WS1(b)` if that proves easier to scope first, despite
   the roadmap listing view materialization first.
3. **`WS2`**: extract task execution/retained work storage from `Sync.*`
   incrementally -- independent of `WS1`, could run in parallel in a
   separate session.
4. **`WS3`**: establish shared-core regression gates -- still worth a
   brief look at whether its gates would have caught anything `WS0`/
   `WS1(a)`'s manual check-feme/CTS verification did not, before treating
   it as moot.
5. **`W0(c)`/`W0(d)`**: classify the local Direct3D test corpus and add an
   explicit FeMe adapter-selection assertion -- a research/documentation
   task, no Windows adapter install needed yet.
6. **`W0(a)`/`W0(b)`** (Direct3D sense): still blocked on a suitable
   Windows host/VM outside this process-isolated container.
7. Backlog carried from prior sessions, still untouched:
   `ENV6(c)(3)`/`ENV6(d)` CTS triage, `ENV7(g)` audit, full CTS
   re-measure.
