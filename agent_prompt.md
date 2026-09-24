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

1. **(~1-2 hrs, well-scoped, recommend starting here)** `L131`: **Fix residual
   graphics push-constant range/indexing cases.** 6 failures in
   `pipeline.monolithic.push_constant`, all dynamic-index/range-size variants
   (53 pass, 6 NotSupported already; the original crash this row used to also
   cover, `count_1_shader_vert_frag_command2`, is already fixed by `L96`, so
   this is narrowed to exactly the 6 ordinary mismatches). Files already
   pinpointed by a prior session's scoping:
   `feme/lib/Transforms/CPU/SPIRVPushConstantLowering.cpp`,
   `feme/lib/Vulkan/GraphicsPipeline.cpp`. Small, concrete, self-contained -- a
   good next pickup, same shape as `L154` was.
2. If `L131` gets picked up and its own investigation turns up something bigger
   than expected (as several `L1xx` rows have), scope the remainder as a new
   top-level row rather than nesting a letter under `L131` -- keep following the
   one-lowercase-letter-deep rule.
3. **(~5 min)** `/tmp/ctsrun_l154/` (this session's own scratch: `caselist.txt`,
   `results.qpa`, `results_full.qpa`) can be deleted -- both runs' findings are
   already fully quoted in `VulkanCTSReport.md`'s new section above; nothing in
   it is referenced from anywhere else.
