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

1. **(first thing, under 2 minutes, every future session until done)** Check
   `env4-full-audit/qpa`/`logs` growth and `ps aux | grep deqp-vk` to confirm
   the detached run is still alive and progressing; if it died, check
   `driver.log` for why and consider restarting from the point it left off (the
   script has its own recovery-batch handling, but a fully-dead parent process
   needs manual relaunch).
2. **(ongoing, do not do until `ENV4` finishes)** No `ninja`/build work on this
   host while the run is active — contention previously caused real SIGSEGV
   crashes under a 17-worker overcommit; the same risk applies to any other
   CPU-heavy concurrent job.
3. **(once the initial 1,803-batch pass finishes)** Let `run_vulkan_cts.py`'s
   own recovery/solo-verification rounds run to completion (`ENV4(b)`), then
   write up full Pass/Fail/NotSupported/Crashed totals and remeasure
   `L417`-`L419` in `VulkanCTSReport.md` (`ENV4(c)`); strike `ENV4` in
   `Roadmap.md` if it's fully clean.
4. **(after `ENV4` closes, ~build is safe again)** `ENV5`: start with `ENV5(e)`
   (`info.device_mandatory_features`, 5 cases including the mandatory-feature
   gap the user explicitly called out) or `ENV5(b)`'s
   `compute_shader_derivatives` cluster (12 cases, largest single compute
   sub-group) — both are concretely scoped, ready to pick up without further
   triage.
5. **(low priority, unchanged)** Upstream the 3 `offload-test-suite` patches if
   push access ever becomes available.g
