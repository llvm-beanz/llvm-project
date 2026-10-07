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

1. **~1 day, dedicated design session, unchanged, carried over yet
   again**: `L375` (`DiamondFlattener` double-visitation of ordinary merge
   blocks). Still the only standing design-session item, now carried
   across four sessions. Do not retry ad hoc — `Roadmap.md`'s `L375` entry
   has the full root-cause writeup and two candidate designs (structurally
   prevent the overlap, or make the memo re-threadable). Pick one and
   actually design it before touching code.
2. **Needs its own dedicated investigation with a memory/handle-counting
   tool**: no new item surfaced this session matching this shape — `L392`
   itself (and now its fuzzer follow-up) is fully closed. If a future
   session wants a similarly tractable target, the next candidate is the
   remaining 20 `DeviceLost` results (12 `pipeline`, 8 `robustness`) noted
   in `VulkanCTSReport.md` item 5, still untriaged, and the 46 `pipeline`
   `SIGSEGV`s flagged as "the next stability-triage priority" repeatedly
   but not yet started with case names recorded.
3. `renderpasses.dynamic_rendering.*` triage: **closed, no further action
   needed** — confirmed 0 Fail this session, matching the prior session's
   own full-family sweep. Do not re-open without new evidence of a
   regression.
4. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache-persistence item: already
   resolved/documented, no action needed.
