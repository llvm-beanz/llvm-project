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
1. Triage `cosh-return-inf-unused` as its own shape (now confirmed still
   in the `ENV6(c)(5)` header-exit-to-latch sub-bucket, 8 cases this
   session) -- carried forward unstarted for three sessions now.
2. The new 3-case no-diagnostic bucket from this session (listed above)
   is completely unexplored -- first step would be
   `FEME_VULKAN_LOG_CREATION_ERRORS=1` re-runs per `L508`'s own method,
   to check whether it's silent-rendering or a logged-but-unmatched
   diagnostic.
3. Chase down why the "pass-through to/from exit check" bucket grew
   (5 -> 10) instead of only shrinking -- likely an artifact of some
   earlier session's fix letting cases progress further before bailing
   out, but not confirmed. Pick one of the 5 newly-appearing cases and
   check whether it used to hit a *different*, now-fixed diagnostic.
4. `GetAlreadyLinearizedCycleExit`'s general-mechanism audit (carried
   from `L540`, still not done) now has an accurate, current 69-case
   `ENV6(c)(3)` list to check against instead of a stale one.
5. Keep re-checking the offload-test-suite patch set each session per
   standing instructions (re-verified clean again this session).
