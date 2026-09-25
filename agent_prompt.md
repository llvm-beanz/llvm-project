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

1. **(large, still genuinely open, now more precisely scoped)** The
   *aggregate*-typed side of the PHINode gap is confirmed **not**
   reachable today (see above -- no unconditional force-decomposing
   producer can populate `WidenedAggregateComponents`), so the
   "PHINode two-pass structural gap" item can likely be considered
   **closed for the force-decompose-producer sub-case** entirely now
   (scalar: `H107`/`L118`; vector: this session's `L195`; aggregate:
   provably unreachable). If a *new* unconditional force-decomposing
   producer is ever added to `widenInstruction`'s dispatch (a 4th one,
   beyond the 3 `isUnconditionallyForceWidenedProducer`-style producers
   this row's own investigation enumerated), remember to check whether
   it can produce an aggregate result and, if so, extend this same
   cleanup step a third way.
2. **(large, deferred many sessions now)** "Provably uniform by
   construction" value tracking for `LoopLinearizer` -- `L188`'s own
   still-open nested-cycle root cause. This is now the single largest
   standing item; a future session should treat it as its own dedicated
   design session, not another incremental poke.
3. **Scan `Roadmap.md` fresh** if not picking up 1-2 above -- the
   long-stale candidate list (`L116(b)`/`L116(f)`, `L126(a)`, `L147`,
   `L98(b)`, assorted `R`/`V`/`W`-prefixed rows) is still individually
   unvetted after many sessions of deferral.
4. **(~5 min)** No `/tmp` scratch remains from this session --
   `/tmp/ctsrun_l195/` (sweep script, per-case logs) removed.

Next step if resuming: item 2 (`LoopLinearizer` uniform-value tracking)
is the most substantive remaining "large" item and deserves a session of
its own dedicated design work before any implementation attempt.
