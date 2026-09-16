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

1. **~1 hour: individually triage the 28 failures above with
   `FEME_VULKAN_LOG_CREATION_ERRORS=1`, one at a time, via the
   `offloader` binary directly (not `llvm-lit -sv` — the env var
   doesn't surface through lit's own capture).** Don't assume any two
   share a root cause without checking — this project has been burned
   by that assumption repeatedly (see every prior session's
   `InterlockedAdd`/`Ddx*` groupings that turned out wrong or
   incomplete). Start with the 8 names not seen in any prior list
   (above) since they're totally unknown quantities.
2. **~10 minutes: confirm `Feature/PushConstant/array_of_matrices.test`'s
   unexpected pass** — run it standalone, check whether it's stable
   across 2-3 repeats, then remove its `XFAIL` annotation if genuine.
3. **H124e** (large, unchanged for many sessions):
   `feme-cpu-simdize`/`feme-cpu-linearize`/`feme-cpu-wrap-entry`
   divergence-handling gaps. The `InterlockedExchange.resources.32.test`
   failure this session shows a *new* diagnostic shape worth checking
   against this bucket: `"loop at '' has more than one divergent exit
   check ('' and ''); unsupported (roadmap milestone 6 deviation)"` —
   may or may not be the same root cause as the rest of H124e's
   already-tracked cases; don't assume, verify first.
4. **H124d** (large, deprioritized): upstream MLIR SPIR-V dialect ops
   for `OpDPdx`/`OpDPdy`/`OpFwidth` — likely still the root cause
   behind `DdxCoarse`/`DdyCoarse`/`ddx_fine`/`ddy_fine`/`fwidth.test`
   above; confirm the connection during step 1's triage pass rather
   than assuming it.
5. **`shaderImageGatherExtended`** (large, carried over many sessions,
   still not filed as its own roadmap row): blocks every
   `dEQP-VK.glsl.texture_gather.*` CTS case. FeMe's own gather is
   `ConstOffset`-only, never true per-invocation dynamic offset —
   advertising this feature honestly is itself a real, separate,
   likely-multi-session capability addition. File a roadmap row before
   starting.
6. Lower priority, deferred 22+ sessions now:
   `transform_feedback.fuzz.random_geometry.all_instance_array.12`'s
   pre-existing heap corruption — `valgrind`'s own trace points at
   `buildStageStorage`/`executeDraws` allocating a too-small buffer.
