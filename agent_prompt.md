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

1. **(~30-60 min)** `L152`: root-cause `subgroupbroadcast_nonconst_*` (105/112 failing, every
   `requiredsubgroupsize`). Start with a single-case repro
   (`subgroupbroadcast_nonconst_int_requiredsubgroupsize4` is likely simplest) via `deqp-vk`,
   then the same `glslangValidator`/`feme-run` fast-iteration pattern this session and `L148`
   both used -- dump actual vs. expected before touching code. It shares the `L148`-added
   `llvm.spv.wave.broadcast`/`WaveCallKind::Broadcast` path (112/112 passing for the constant-
   index form), so the bug is likely specific to how a non-constant-but-uniform index reaches
   `lowerBroadcast`, not the merge-mask bug this session just fixed.
2. **`binding_model.shader_access`** (11,834 cases, `L147`'s last big untriaged cluster) --
   still wants its own dedicated session given the scale. Nothing this session changes that.
3. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing) -- still the largest
   not-yet-started cross-repo item, for a session wanting a change of pace from CTS triage.
4. ~~Worth double-checking: does the fix generalize to a cycle nested two diamonds deep~~ --
   checked this session with a quick, uncommitted `feme-opt` probe (`if (a) { if (b) { <loop>;
   } } <store>`): the store correctly threads through both merges (`sideeffect.merge6 =
   select(c1, sideeffect.merge, sideeffect.f)`, where `sideeffect.merge` is itself the *inner*
   diamond's own `select(c2, ...)`). No issue found; nothing further needed here.
