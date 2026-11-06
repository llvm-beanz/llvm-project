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

Next steps, ranked:
1. Re-measure the `ENV6(c)(5)`/`ENV6(c)(6)` roadmap cluster counts --
   still an open item from two sessions ago, not touched this session
   either (this session's own corpus diff only exercised the targeted
   case plus whatever `graphicsfuzz.*` already covers; the cluster
   counts in `ENV6(c)(5)`/`ENV6(c)(6)`'s own roadmap rows predate even
   `ENV6(c)(5a)`'s fix and have never been directly re-verified).
2. Triage `cosh-return-inf-unused` as its own shape (two distinct "only
   a uniform pass-through region" bail-outs plus a mask-affecting-op
   diagnostic -- confirmed by two sessions now to be unrelated to both
   `ENV6(c)(5a)`'s and this session's fix).
3. The `GetAlreadyLinearizedCycleExit` fix this session landed is a
   general mechanism (any already-uncond-ified child header, not just
   this one shape) -- worth a quick audit of whether any other
   known-unfixed `ENV6(c)(3)`/`ENV6(c)(5)`/`ENV6(c)(6)` cluster case
   happens to share the exact "enclosing loop's walk reaches an
   already-linearized nested loop's own header" shape and gets fixed
   incidentally; the 757-case corpus diff this session found exactly
   one flip, so if there's overlap it's likely small, but not yet
   checked directly against the untriaged clusters' own case lists.
4. None of the offload-test-suite patch set is at risk right now (all 5
   re-verified this session with a fresh `llvm-beanz/feme` fetch), but
   keep re-checking each session per standing instructions -- upstream
   `WaveActiveMax.test` content (not just line endings) has drifted
   once before and may again.
