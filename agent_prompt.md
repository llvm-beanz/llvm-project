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

1. **H138** (~1-2 hours, filed this session): teach
   `ResourceLoweringPass` to be ABI-aware for `v3i64`/`v4i64` raw
   resource access (indirect/`sret` calling convention), or decompose a
   3/4-wide i64 raw access into 2 calls against the already-safe
   `v2i64`/scalar primitives from H137. Finishes
   `WaveActiveAllEqual.int64.test`.
2. **The 8 `dEQP-VK.api.device_init.create_device_unsupported_features.*`
   CTS failures** found via this session's spot-check (not yet
   triaged at all -- brand new finding, not previously tracked): worth
   a `FEME_VULKAN_LOG_CREATION_ERRORS=1` pass to see if any share H136's
   own root-cause shape or are something else entirely. Not yet filed
   as a roadmap row.
3. **`WaveOps/WaveActiveMax.test`'s NegInfs mismatch** (unresolved,
   carried over 2+ sessions): expects `[0,0,0,0]` for an all-`-inf`
   input, FeMe produces the more IEEE-correct `[-inf,-inf,-inf,-inf]`.
   `getReduceIdentity` is correct; may be a DXC/real-hardware quirk
   baked into the CTS golden values rather than a FeMe bug -- needs a
   `spirv-dis`-level dig that hasn't happened yet.
4. **`WaveOps/WaveReadLaneAt.mtx.test`** (transpose bug, not yet
   triaged) and **`WaveOps/WaveIsFirstLane.test`** (semantics gap, not
   yet triaged) -- both flagged in this session's bucketing pass but
   not individually root-caused yet.
5. **H124e** (~several sessions, large, unchanged for many sessions):
   `feme-cpu-simdize`/`feme-cpu-linearize`/`feme-cpu-wrap-entry`
   divergence-handling gaps -- may overlap with several of the
   `WaveOps/*` items above; still needs per-case triage, don't assume
   shared cause.
6. **`shaderImageGatherExtended`** (large, carried over several
   sessions, still not filed as its own roadmap row): blocks every
   `dEQP-VK.glsl.texture_gather.*` CTS case. FeMe's gather is
   `ConstOffset`-only -- advertising this honestly is a real, separate,
   multi-session capability addition. File a roadmap row before
   starting.
7. Lower priority, deferred 21+ sessions now: `transform_feedback.
   fuzz.random_geometry.all_instance_array.12`'s pre-existing heap
   corruption -- `valgrind`'s own trace points at `buildStageStorage`/
   `executeDraws` allocating a too-small buffer.
