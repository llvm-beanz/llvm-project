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

1. **Start here** (prototype, ~30-60 min): try fix (a) first --
   thread the enclosing call's own `End` into the recursive `flatten()`/
   `validate()` calls as an extra bound, and have `immediatePostDom`-based
   `R`/`R'` computation reject (fall back to the existing generic
   diagnostic) whenever the computed reconvergence point is not dominated
   by -- or does not itself reach -- the enclosing `End` cleanly. Reproduce
   first with `FEME_DEBUG_FLATTEN_TRACE=1 build/bin/feme-opt --llvm
   -passes="function(instnamer),feme-cpu-linearize" -S
   /tmp/mergesort_discard_repro/repro.ll -o /dev/null` to re-confirm the
   `bb584`/`bb608`/`bb589`/`Flow21` names before touching code (names are
   stable given `instnamer`, no need to re-derive).
2. If (a) turns out to reject too much (e.g. breaks other currently-passing
   nested-diamond cases), fall back to (b): extend `validate`'s per-block
   bookkeeping so a pass-through block with >1 predecessor gets the same
   `hasNPredecessors`/ownership check a `CondBr`'s own `R` does, rejecting
   (not silently allowing) an inner diamond whose own `T`/`F` walk crosses
   such a block before its own `End`.
3. Whichever lands: new regression unit test in `LinearizeTest.cpp`, reduced
   from `/tmp/mergesort_discard_repro/repro.ll` (same style as `L509`/`L510`/
   `L511`'s own tests), asserting clean `verifyModule` (or, if landing (a)/
   (b) alone only makes this a clean diagnostic rather than a full lowering,
   assert the diagnostic -- confirm which outcome is actually achievable
   before writing the assertion).
4. Re-run the full 106-case `graphicsfuzz` corpus after any fix -- this is
   exactly the kind of change that could have wide-reaching effects on
   other nested-diamond shapes, not just this one case; do **not** declare
   victory on the 1 targeted case alone.
5. Rebuild `check-feme`, confirm 0 regressions, then update
   `VulkanCTSReport.md` (new `L513` entry) and strike `ENV6(c)(2)(h)` in
   `Roadmap.md` only if the case actually flips to Pass.
6. Carried-forward, unchanged, lower priority: `ENV6(c)(2)(f)` (1 case,
   unexplained non-dominance `Fail`), `ENV6(c)(2)(a)` (7-case all-black-
   render bucket, not discard-centric), `ENV6(c)(2)(c)`/`(d)` (2 isolated
   1-case issues), `ENV6(c)(3)` (83 cases), `ENV6(d)` (132 cases),
   `ENV5(c)(7)(b)` (hull lockstep barrier, 3 cases), offload-test-suite
   upstreaming (3 patches, blocked on push access).
