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

1. **(~1-2 hrs, well-scoped)** `L193`: root-cause the `SampleCmp.test`
   crash -- get an isolated repro (this test alone, or its own compiled
   `.o` fed straight to `feme-translate`/`feme-opt`), then `spirv-dis`
   the failing call's operand types to find which one the pattern
   wrongly assumes is a vector.
2. **(~1-2 hrs, well-scoped, same shape as `H124u`'s own prior work)**
   `L194`: add a `QueryLod1D`/`QueryLodArray1D` counterpart to
   `H124u`'s own `QueryLodCube` work.
3. **(large, no fix designed, carried from `L191(b)`)** The `PHINode`
   two-pass structural gap -- unaddressed on both the vector and
   aggregate sides.
4. **(large, deferred many sessions now)** "Provably uniform by
   construction" value tracking for `LoopLinearizer` -- `L188`'s own
   still-open nested-cycle root cause.
5. **Scan `Roadmap.md` fresh** if not picking up 1-4 above -- the
   long-stale candidate list (`L116(b)`/`L116(f)`, `L126(a)`, `L147`,
   `L98(b)`, assorted `R`/`V`/`W`-prefixed rows) is still individually
   unvetted.
6. **Worth a broader sanity sweep at some point**: since this session
   found one already-documented fix (`H140`'s `offload-test-suite`
   side) silently lost from the checkout, it's plausible other
   `offload-test-suite`-side fixes from other roadmap rows citing
   "offload-test-suite" as a touched file might have the same problem
   -- a `grep`-for-roadmap-rows-mentioning-offload-test-suite-then-
   verify-each-is-actually-committed-there pass would be cheap
   insurance, though not urgent (only 1 other row currently references
   an `offload-test-suite`-side change, per a quick `grep` this
   session, so the blast radius is probably small).
7. **(~5 min)** No `/tmp` scratch remains from this session --
   `/tmp/ctsrun_l192a/` (case list, per-case sweep driver, raw QPA logs)
   removed. Note: the golden images themselves now live persistently at
   `/home/dev/dev/offload-golden-images` (sibling to `VK-GL-CTS`/
   `offload-test-suite`), not `/tmp` -- do *not* delete that directory,
   the `check-hlsl-feme-vk` build's own `GOLDENIMAGE_DIR` CMake cache
   variable now points there.

Next step if resuming: `L193` (the `SampleCmp.test` crash) is the more
interesting pick -- a real, previously-hidden FeMe bug, well-scoped at
1-2 hours. `L194` is more mechanical (same shape as `H124u`'s own prior
`QueryLodCube` work, just for `Plain1D`). Either is a good, focused next
session.
