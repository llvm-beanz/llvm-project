---
model: claude-sonnet-5.5
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

Please continue working on the FeMe Vulkan ICD. Suggested next steps:

**Next steps, ranked**:
1. `ENV6(c)(9)`: generalize `collectUniformPassThroughRegion`'s
   already-linearized-child-cycle exemption to also recognize a
   post-child-cycle relay/guard block (`loop.exit.guard5` in the pinned
   repro), not just the child's own header -- the pinned
   `unsupported-nested-loop-divergent-inner-exit-guards-discard.ll` repro
   from `L542` is a ready-made trace anchor (`FEME_DEBUG_PASSTHROUGH=1`
   already shows exactly where it gets stuck: transparent through
   `%Flow12`, then correctly-for-now rejected at `%loop.exit.guard5`).
   Likely needs `DiamondFlattener`'s own private
   `isLoopControlOrHeaderRelayEdge`-style logic. This is also the path to
   actually fixing `cosh-return-inf-unused` itself (still `Fail`).
2. The 3-case no-diagnostic bucket from `L541` is still unexplored --
   `FEME_VULKAN_LOG_CREATION_ERRORS=1` re-runs per `L508`'s method.
3. The "pass-through to/from exit check" bucket growth (5 -> 10, from
   `L541`) is still unconfirmed as regression-vs-progress.
4. Keep re-checking the offload-test-suite patch set each session per
   standing instructions (clean again this session; see the stray-branch
   note in step 7 above if it recurs).
