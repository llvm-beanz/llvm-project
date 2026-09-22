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

1. **(highest value, next real Vulkan-correctness item)** `L124(o)`:
   `getMatrixWholeAccess`'s non-wrapper-branch nested-struct walk +
   `getTightNestedStructType`/`getTightMatrixType` widening -- still
   the standing item from several sessions back, untouched by this
   session.
2. `binding_model.shader_access` (11,834 cases, the overwhelming
   majority of `L147`) is still the eventual big one; wants its own
   dedicated session given the scale.
3. `L148` (14-case `subgroups.ballot_broadcast.*.
   requiredsubgroupsize{64,128}` hang cluster) still untouched -- a
   hang, not a crash; expect to need a debugger, not stdout diagnostics.
4. `L125(m)`/`L125(n)` (upstream MLIR+LLVM `ConstOffsets` plumbing) --
   still the largest not-yet-started cross-repo item, for a session
   wanting a change of pace from CTS triage.
5. Worth a 5-minute check next session: do any of `L147`'s other
   `ubo.*` sub-clusters (`random` 134, `2_level_array` 86, etc. --
   though the full `ubo.*` sweep this session came back 0 Failed, so
   this is likely already moot; only worth re-checking if a *future*
   regression reintroduces `ubo.*` fails) share this same
   `DataLayout`-ordering bug shape. Given the full sweep already shows
   0 Failed, this step is probably already done implicitly -- skip
   unless something regresses.
6. No scratch left over this session -- all `/tmp/l150_*`,
   `/tmp/sroa_*`, `/tmp/UnrollDiag*`, and `/tmp/mintest.ll` deleted;
   the one artifact worth keeping (the unit test) is committed, not
   left in `/tmp`.
