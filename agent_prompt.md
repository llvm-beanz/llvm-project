---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

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

# Request

Can you work the H-series milestones?

The last session suggested the next steps:

1. **~30-45 min: individually re-confirm which of H124e's 7 wrap-entry
   cases actually share the exact 3-phi/nested-branch/2-barrier shape**
   found this session (only `InterlockedAdd.32.test` was directly
   inspected) — don't assume the other 6 match without checking, this
   project has been burned by that assumption before.
2. **~1-2 hours, still untouched: `InterlockedCompareExchange.resources.32.test`'s
   `feme-cpu-simdize` divergent-branch gap** — needs an IR-level
   reduction via `feme-opt --feme-convert-spirv-to-llvm` before any fix
   attempt, same methodology as this session's H124e dump.
3. **~1-2 hours, still untouched: `InterlockedExchange.resources.32.test`'s
   `feme-cpu-linearize` multi-exit-loop gap** — same, needs its own
   IR-level reduction first.
4. **Large, deprioritized: H124d** — upstream MLIR SPIR-V dialect
   `OpDPdx`/`OpDPdy`/`OpFwidth` ops, likely root cause of the
   `DdxCoarse`/`DdyCoarse`/`ddx_fine`/`ddy_fine`/`fwidth.test` group (5
   failures) — still not individually confirmed across many sessions now.
5. **Large, not yet filed as its own roadmap row: `shaderImageGatherExtended`**
   — FeMe's gather is `ConstOffset`-only, blocks every
   `dEQP-VK.glsl.texture_gather.*` CTS case. File the row before starting.
6. **Lowest priority, deferred 26+ sessions: `transform_feedback.fuzz.random_geometry.all_instance_array.12`'s**
   pre-existing heap corruption (valgrind points at
   `buildStageStorage`/`executeDraws`).
