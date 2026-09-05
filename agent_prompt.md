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

Can you work on L40 or other prerequisites blocking the L-series milestones?

> **A real CTS re-run of L30's own fix surfaced a distinct, unrelated
> control-flow-linearization gap**: `dEQP-VK.mesh_shader.ext.misc.payload_read`
> (task-payload write in the task/amplification stage, read back in the mesh
> stage -- the exact real-hardware shape L30's own fix targets) fails
> `vkCreateGraphicsPipelines` with `"feme-cpu-linearize: function 'main': loop
> at '' has an internal branch in ''; unsupported (roadmap milestone 6
> deviation)"`, while its sibling `payload_not_accessed` case (which never reads
> the payload back) passes -- confirming L30's own fix works for a real CTS
> mesh-shader payload read too, and this is a genuinely separate, later gap.
> `feme-cpu-linearize`'s own loop-internal-branch rejection is a known family
> with more than one distinct sub-shape: H19k already fixed one narrow syntactic
> shape (a redundant `StructurizeCFGPass`-inserted `Flow` block re-deriving an
> already-decided uniform trip-count check via a phi of two literal constants),
> but `payload_read`'s own loop evidently has a structurally different
> internal-branch shape H19k's own narrow, conservative fold does not match (its
> own precondition -- a `CondBr` fed by a phi of exactly two literal
> `ConstantInt`s -- is intentionally strict to avoid misfiring on genuine
> divergent control flow). Needs its own real IR reduction of this exact CTS
> shader (`glslangValidator`/`feme-translate`/`feme-opt`, mirroring H19k's own
> reduction technique) to identify the new internal-branch shape and whether it
> needs its own new fold in `LoopLinearizer` or a genuinely different approach
