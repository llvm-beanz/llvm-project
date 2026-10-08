---
model: claude-sonnet-5
resume: db99d377-bdef-4dd8-a2b5-787bcf66bc23
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

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~1 day, dedicated session)** `L398`: `QueueExecutor`/`Fence` teardown
   deadlock — unchanged from prior handoffs. Build a *spec-valid* reproducer
   (`vkDeviceWaitIdle` before destroying anything but the fence itself) to
   isolate the fence-specific race from the broader object-lifetime class a
   prior reproducer attempt accidentally also hit.
2. **(~30 min)** Cluster the global Fail/ResourceError cases from a *fresh* full
   sweep with a flat per-case result list (a real `.qpa`/`txt-caselist` export)
   — the Oct-7 `report.txt` was confirmed in an earlier session to be a
   diff/comparison artifact unsuitable for direct clustering; the clean
   `renderpasses.*`-only sweep this session ran
   (`/tmp/l401_regression_renderpasses_stdout.txt`) is usable for that one group
   right now if someone wants to pre-triage it before a full-case-space run.
3. **(~several hours, background)** A fresh full-case-space CTS sweep (last
   genuine one: `feme-20261005-full-1170553-complete`, Oct 5) — this session
   only ran the 2 `L401` cases plus one `renderpasses.*` group (81,188 cases),
   not the full ~1.17-1.64M case space. Running one is the only way to surface
   the next batch of untriaged failures across groups other than `renderpasses`.
4. `LLVM_EXTERNAL_PROJECTS=OffloadTest` cache item: already resolved (see
   `L380`), no action needed, carried forward unchanged for the record only.
