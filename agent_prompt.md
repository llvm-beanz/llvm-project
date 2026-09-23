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

1. **`binding_model.shader_access`** (11,834 cases, `L147`'s last big untriaged cluster) --
   still wants its own dedicated session given the scale. With `ballot_broadcast.*` now fully
   closed, this is the single largest remaining known-failing CTS cluster.
2. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing) -- still the largest
   not-yet-started cross-repo item, for a session wanting a change of pace from CTS triage.
3. **(~15 min)** Worth a quick sanity pass next session: re-run the full `subgroups.*` sweep
   once more from a clean build to confirm the "zero fails" result is stable (this session's
   sweep completed without the prior session's unrelated `.amber`-file-not-found harness abort,
   so it's the first time the *entire* cluster has been swept end-to-end in one run -- worth one
   more confirmation before treating "0 known fails in `subgroups.*`" as fully settled).
4. With `subgroups.*` fully green, consider broadening the next CTS sweep beyond
   `subgroups.*`/`ubo.*`/`binding_model.*` to find the next-largest untriaged cluster overall --
   no specific candidate identified yet this session, but worth a `deqp-vk --deqp-case='dEQP-VK.*'`
   totals-only pass (no full log) to rank remaining clusters by failure count before picking one.
