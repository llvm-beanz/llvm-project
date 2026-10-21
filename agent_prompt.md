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

1. **(next large untriaged block, ~1-2 hrs to triage)** `ENV5(c)`: 38 untriaged
   `graphics.*` cases (`float16` 10, `variable_init` 10, `unused_variables` 8,
   `barrier` 5, `module` 4, `indexing` 1) — no logs/build done yet for this
   block in any session so far. `float16` and `variable_init` are the largest
   clusters, good first targets. Same triage pattern as every prior cluster:
   `FEME_VULKAN_LOG_CREATION_ERRORS=1`/direct `deqp-vk --deqp-case=...` before
   guessing at a fix.
2. **(low priority, structural, revisit only if a design change opens it up)**
   `opunreachable.all`: the linearizer does not support unreachable-terminated
   blocks (tracked since `ENV3`'s `assertion-crash-cases.txt`). Not expected to
   be a quick fix; don't start here next session unless `ENV5(c)` is fully
   closed first.
3. **(next dedicated clean-host block, no competing builds)** `ENV4`: full
   current CTS corpus collection, still running uninterrupted across many
   sessions (`initial-01187` and climbing as of this session's last check). Keep
   checking `ps aux | grep deqp-vk` and batch-file growth each session; let it
   finish on its own before writing up final Pass/Fail/NotSupported/Crashed
   totals and striking `ENV4` in `Roadmap.md`.
4. **(low priority, unchanged across many sessions)** Upstream the 3
   `offload-test-suite` patches (`Image.cpp` underflow, `Color.h` NaN-clamp
   assert, `lit.cfg.py`'s dropped `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS`) to
   `llvm-beanz/offload-test-suite` if push access ever becomes available.

**Next action (under two minutes):** `cd
/home/dev/dev/VK-GL-CTS/build/external/vulkancts/modules/vulkan &&
FEME_VULKAN_LOG_CREATION_ERRORS=1 ./deqp-vk
--deqp-case='dEQP-VK.spirv_assembly.instruction.graphics.float16.scalar'
--deqp-log-images=disable` (or the nearest matching real `graphics.float16.*`
case name) to get the first real error string for `ENV5(c)`'s largest untriaged
cluster.

