---
model: claude-sonnet-5
resume: ec2f5570-263a-4b95-917f-6c2230e594cf
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
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L39 or other prerequisites blocking the L-series milestones?

> **`SimpleAmplification.test` now clears L30's own JIT-symbol fix and reaches
> `feme-cpu-simdize` for the first time, but `vkCreateGraphicsPipelines` still
> fails there instead**: `"error: feme-cpu-simdize: function 'main' has a
> divergent value '' of vector type; only a constant-index insertelement chain,
> a phi, a select, a shufflevector, elementwise arithmetic/cast, a vector
> comparison, a homogeneous vectorizable intrinsic call, or a
> resource/image/ordinary load is supported (roadmap milestone 7 deviation)"`,
> `gpu-exec: error: Failed to create mesh shader pipeline. (VkResult = -3)`. The
> amplification shader's own `groupshared Payload gs_payload` write plausibly
> stores a genuinely vector-typed (not fully scalar-decomposed) value straight
> into the payload -- unlike an ordinary stage-IO store, whose value always
> reaches `canonicalizeSPIRVStage` pre-decomposed to a scalar leaf, a task
> payload write's fallback canonicalization (L30) wraps whatever type the raw
> SPIR-V-derived store already has, verbatim, which may leave a small vector
> (e.g. a `float3`/`float4` payload member) unscalarized -- landing on a
> `SIMDize.cpp` divergent-vector-of-vector-type shape
> `FunctionWidener::getWidened`'s own generic single-scalar-per-lane widening
> was never designed to accept (see its own assert, `getWidened does not support
> a vector-typed value; use getVectorComponents instead`). Needs its own real IR
> reduction of this exact case (isolating which payload member's own HLSL type
> triggers this) to confirm whether the fix belongs in
> `widenMaskedTaskPayloadStore`/`widenStageOp` (routing a vector-typed payload
> value's operand through `getVectorComponents` the way an ordinary vector-typed
> store operand already is elsewhere) or further upstream in how
> `CanonicalizeStage.cpp`'s task-payload fallback itself decomposes a stored
> value's type before ever reaching this pass
