---
model: claude-sonnet-5
resume: b8ce7a07-6a6f-4205-864b-b09621346aaf
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

The last session stalled out. Any intermediate states are stoerd in a git stash.

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~1-2 days)** `K6q`: 2 cases, handle-level `OpPhi`/`OpSelect` merge
   (merging raw resource *handles* before `getpointer` is called, not pointers
   already derived from one). Deeper and architecturally distinct from this
   session's fix -- `hasOnlySupportedUses` (the handle-level checker) would need
   the `DescriptorIndex` itself to become phi-mergeable before any GEP/offset is
   known. Smallest case count of the three new buckets but possibly the hardest.
2. **(~half day)** `K6r`: 2 cases, storing a handle-derived pointer as a store's
   *value* operand (not its pointer operand) -- `hasOnlySupportedPointerUses`
   only checks the pointer-operand position today. Likely the smallest, most
   contained of the three new buckets -- good first pick for a fast win.
3. **(~1 day)** `K6s`: 5 cases, frag/tessc/tesse result-correctness bug --
   pipeline builds and runs, output is wrong, zero normalization rejection.
   Needs its own investigation (not a legalization gap at all); start with a
   minimal single-buffer frag repro and CPU-backend tracing.
4. **(~1-2 days, "issue outside FeMe", standing)** `K6p`: 14-15 cases,
   `OpPtrEqual`/`OpPtrNotEqual`/`OpPtrDiff` (opcodes 401/402/403) entirely
   undefined in MLIR's SPIR-V dialect TableGen. Not attempted again this session
   (scope stayed on `K6n`).
5. **(standing, unresolved many sessions)** `backup-before-rebase-20261014` tag
   decision, `CMAKE_BUILD_TYPE=RelWithDebInfo` vs `Release` decision.
6. **(standing)** `L419(j)` remaining untriaged items, `L418(a)` sanity
   re-check, deferred full `Vulkan14FeatureInventory.md`/extension-manifest
   regeneration.
7. **(standing)** offload-test-suite `VkBool32`/`WaveActiveMax` pattern check --
   not exercised this session either, no `check-hlsl-feme-vk` work done (session
   stayed scoped to `K6n`'s compiler fix).

**Immediate next action for next session:** pick `K6r` (2 cases, single-function
fix in `hasOnlySupportedPointerUses`'s store-operand check -- smallest, most
contained of the three new buckets) for a fast win, or `K6s` if a correctness
bug feels more interesting than another legalization gap.
