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

1. **(~30-60 min)** `ENV5(a)`'s remaining 6 cases:
   `opselect.array_stride_select` (1, custom-array-stride constant-legalization
   gap, `failed to legalize 'spirv.Constant'`) and `entrypoint.tess_con_*` (3,
   likely tessellation gap, no error logged -- needs a real build+trace, not
   log-mining). `opptrequal`/`opptrnotequal` (2) is already-known-structural
   (`feme-cpu-simdize` groupshared-global limitation), not a quick fix.
2. **(~1-2 hours, needs real build+trace)** `ENV5(b)`'s remaining 24 cases:
   `compute_shader_derivatives.lod_op.*` (9, numeric) and 15 untriaged
   `compute.*` cases.
3. **(next dedicated clean-host block, no competing builds)** `ENV4`: full
   current CTS corpus collection. The 6 background workers were paused (SIGSTOP)
   by a prior session to allow this session's builds; resumed via `kill -CONT`
   at the end of this session -- confirmed all 6 back to `Rl` (running) state.
   Let it keep progressing; do not start another build while it's mid-run.
4. **(low priority, unchanged)** Upstream the 3 `offload-test-suite` patches
   (`Image.cpp` underflow, `Color.h` NaN-clamp assert, `lit.cfg.py`'s dropped
   `FEME_VULKAN_SAFETY_NET_TIMEOUT_MS`) to `llvm-beanz/offload-test-suite` if
   push access ever becomes available.
5. **(worth a quick check, ~15 min, carried forward from a prior session)** A
   broader regression sweep for any other
   `CopyMemory`-on-eagerly-loaded-scalar-`Input` shape in the untriaged
   `compute.*`/`graphics.*` failure lists -- the trivial vertex-shader
   passthrough pattern L472 fixed is common to many Amber-based tests, not just
   `terminate_invocation`.
