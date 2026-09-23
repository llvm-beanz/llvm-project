---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

Can you please work on the FeMe ICD implementation? The previous session gave
the next steps:

1. **(~1-2 hrs, highest value)** `L180`: root-cause `descriptorset_
   random`'s remaining 118 image-verification (pixel-mismatch) failures
   -- confirmed a separate bug class from `L178`/`L179` (these 118 were
   already failing pre-session, untouched by either fix). Stage-suffix
   breakdown so far: 30 `.frag.*`, 22 `.vert.*`, 22 `.comp.*` (74 of
   118; ~44 need their own suffix breakdown, not done this session).
   Pick the smallest failing case per stage bucket, dump actual-vs-
   expected pixels first -- `L175`'s own history is a specific warning
   against assuming one root cause too early across sub-shapes that
   only share a failure symptom.
2. **(~15 min)** Still not done, mentioned by the last two sessions:
   add an `assert`/`opt -passes=verify` step after
   `SPIRVUnmergeResourceLoadsPass` runs in debug builds --
   `SPIRVToLLVMPatterns.cpp` just found its own second and third
   found-by-CTS-not-by-review latent bugs (`L178`/`L179`), suggesting
   this class of "pass declines silently, only a much later stage
   fails" gap is worth a general defensive check, not just in the one
   pass it was originally floated for.
3. **`inline_uniform_blocks` (9 fails)** -- still never-triaged, smaller
   than `descriptorset_random`, good if `L180` feels too big to start
   cold.
4. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing)
   -- still the largest not-yet-started cross-repo item.
5. **(~5 min)** `/tmp` cleanup needed: `dsr_*` logs/qpa files,
   `Pipeline.cpp.bak`, `pipeline_fail_cases*.txt`, `dsr_remaining_fails.
   txt`, and the large stdout-capture temp files under
   `/tmp/*-copilot-tool-output-*` this session generated -- everything
   worth keeping is already quoted in `VulkanCTSReport.md`/this file.
