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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you close out L37 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`Feature/Semantics/{DomainSystemValues,HullSystemValues}.test` (L27's own
> two cases) now clear the `feme-cpu-simdize` divergent-vector-decomposition gap
> entirely but both still fail `vkCreateGraphicsPipelines`, `VkResult = -3`, on
> the *same* `feme-cpu-wrap-hull: control-point phase only supports a control
> point reading its own input control point's attributes` diagnostic text as the
> already-closed `H29g` -- but a real, distinct root cause this time, confirmed
> via a temporary pre-`HullWrapperPass` IR dump: `HullSystemValues.test`'s
> `patch[i].position` (a genuine self-indexed `InputPatch` read, `i` being
> `SV_OutputControlPointID`) does not lower to a single `feme.stage.input.load`
> call carrying the dynamic self-index as its control-point-index operand at all
> -- by the time `SIMDizePass` (which runs *before* `HullWrapperPass`, see
> `runPipeline`'s "widening" then "wrapping" order) has finished, the earlier
> SPIR-V-import/legalization passes have already fully unrolled the
> `InputPatch<HSInput,3>` read into 12 separate, purely-*constant*-indexed
> `feme.stage.input.load` calls (one per (component, control-point) pair,
> control-point index literals `0`/`1`/`2`), which the shader's own code then
> selects among *after* loading using the dynamic self-index -- a valid
> "materialize-then-select" lowering of a dynamic `InputPatch` index that
> `lowerHullInputLoad`'s self-index-or-zero check was never designed to
> recognize, since its check inspects each individual call's own (now
> always-constant, never-`0` for control points 1/2) control-point-index operand
> directly, rejecting the literal `1`/`2` cases outright even though
> `computeStageStorageAddress` never actually *uses* that operand's value at all
> (it always addresses storage via *this* invocation's own flat index,
> discarding the checked operand entirely once validated) -- meaning a
> literal-constant *other*-control-point **input** read (always legal and always
> available up-front, unlike a same-restriction on **output** data, which
> genuinely has a same-invocation-only correctness reason) needs its own new
> addressing path (deriving the target flat invocation index from the literal
> control-point-within-patch offset plus `HEnv.InputPatchControlPointCount`,
> rather than reusing "this invocation's own" unconditionally) before this
> diagnostic can stop over-rejecting a case its own `computeStageStorageAddress`
> machinery could handle correctly today. Needs its own design note in
> `FeMeCPUDesign.md` once scoped (a genuine addressing-scheme extension, not a
> one-line fix)
