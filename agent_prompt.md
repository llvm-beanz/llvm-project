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

1. **(~20-30 min)** Write a minimal standalone `.ll` reproducer for the
   graphicsfuzz terminator-assertion bug (use
   `FEME_DUMP_IR_PRELINEARIZE=1` against the real `.spv` first, like this
   session did, to capture the exact crashing IR before distilling it).
2. **(~30-45 min)** Fix `DiamondFlattener::flattenLoopBodyRegion`'s `nullopt`
   path so the block it bails out of always has a valid terminator before the
   "unsupported" diagnostic returns — likely needs either a transactional
   erase/insert ordering or an explicit terminator-restore step on the bailout
   path. Add a lit + unit test mirroring this session's pattern.
3. **(~15 min)** Re-run all 6 original `assertion-crash-cases.txt` entries
   plus `check-feme` to confirm the full ENV3 acceptance bar ("six idle-host
   crash reproducers completing without abort") is finally met; update
   `Roadmap.md`'s `ENV3`/`ENV3(a)` rows (strike through if fully closed) and
   `VulkanCTSReport.md`.
4. **(~30-60 min, separate session recommended)** Pick up Clang Mandelbrot:
   get a fresh backtrace/IR dump of the `feme-cpu-simdize` divergent-branch
   diagnostic, confirm whether it's related to the same `UniformityInfo`
   snapshot-staleness class of bug just fixed here (plausible, worth checking
   first) or genuinely separate.
5. After ENV3 is fully closed: move to ENV4 (full corpus collection with the
   corrected runner) — this needs a clean host with no competing builds/offload
   jobs, so schedule it as a dedicated block, not an afterthought at the end of
   a bug-fixing session.
