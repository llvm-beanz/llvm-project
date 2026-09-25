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

1. **Bug 4 (LCSSA violation) is the next blocker for non-leaf
   traversal.** Reproduces reliably (not flaky like bug 3 was) via a
   single case: `deqp-vk --deqp-case='dEQP-VK.graphicsfuzz.cov-nested-
   loop-large-array-index-using-vector-components'` with non-leaf
   traversal temporarily re-enabled (see this session's reverted
   one-line change: replace `if (CI.children(C).empty())
   Changed |= linearizeCycle(C);` with an unconditional
   `Changed |= linearizeCycle(C);` in `linearizeCyclePostOrder`).
   Budget ~2-3 hrs: find which value `linearizeCycle`'s own new-block
   insertion (masked continue/break guards, relay hops) fails to give a
   proper exit-block phi once an *enclosing*, not-yet-linearized cycle
   is involved -- a shape leaf-only cycles never exercised. A minimal
   repro (llvm-reduce on the JIT'd IR, not the full shader) would help a
   lot here given the reliable repro.
2. **Once bug 4 is fixed, actually flip on non-leaf traversal** (remove
   the `CI.children(C).empty()` guard for real) and update
   `LinearizeTest.LinearizesInnerLeafLoopButLeavesOuterNonLeafLoopAlone`
   to match the new, correct behavior (this session saw exactly what
   that looks like: 2 mask-any reductions instead of 1, a real
   `loop.continue3` condition instead of `outer.break`) -- don't just
   delete the test, update its expectations.
3. **`Roadmap.md` full-table sweep** (`L116(b)`/`L116(f)`, `L126(a)`,
   `L147`, `L98(b)`, assorted `R`/`V`/`W`-prefixed rows) is still
   individually unvetted after many sessions of deferral -- still a
   valid change-of-pace option.
4. `/tmp` scratch is clean -- nothing left over from this session.
