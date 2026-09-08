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

Can you close out L77 from the roadmap or other prerequisites blocking the
L-series milestones?

> **The tessellation-evaluation (domain) shader stage of a real DXC-compiled
> hull/domain pair declares no tessellation domain execution mode**, discovered
> by this session's (L37's) own real
> `Feature/Semantics/{DomainSystemValues,HullSystemValues}.test` re-run once
> L37's fix let both cases clear `HullWrapperPass` for the first time and reach
> `vkCreateGraphicsPipelines`'s later tessellation-state-merging check for the
> first time too: `GraphicsPipeline.cpp` rejects with `"the
> tessellation-evaluation stage declares no tessellation domain execution mode
> (Triangles/Quads/Isolines)"` even though the domain shader's own compiled
> SPIR-V genuinely has `OpExecutionMode %main Triangles` (confirmed via
> `spirv-dis` on the real `.o` this test compiles) -- the domain entry is
> missing only `SpacingEqual`/`VertexOrderCw`/etc, not `Triangles` itself. Root
> cause (confirmed via `spirv-dis` on both this test's real hull and domain
> SPIR-V binaries): a real `dxc -spirv` compile of an HLSL hull/domain pair puts
> *every* tessellation execution mode
> (`Triangles`/`SpacingEqual`/`VertexOrderCw`/`OutputVertices`) on the **hull
> (`TessellationControl`)** entry point (the one whose HLSL source actually
> wrote `[domain("tri")] [partitioning("integer")]
> [outputtopology("triangle_cw")]`), duplicating only `Triangles` onto the
> **domain (`TessellationEvaluation`)** entry -- not the Khronos-spec-implied
> split (domain-shape/spacing/vertex-order modes on the tessellation-evaluation
> entry, output control point count on the tessellation-control entry)
> `ConvertSPIRVToLLVMPass.cpp`'s importer assumed. That importer's per-function
> `Info.TessDomain`/`Info.TessPartitioning`/`Info.TessOutputPrimitive`
> bookkeeping is keyed strictly per entry point, and the code that finally sets
> the `feme.tessellation.domain` function attribute (around line 574) requires
> *both* `TessDomain` *and* `TessPartitioning` to be present on the *same* entry
> before setting it -- so the domain entry, which only ever sees `Triangles` and
> never `SpacingEqual`/`VertexOrderCw` from this real DXC output shape, never
> gets the attribute at all, and `GraphicsPipeline.cpp`'s `DomainState` stays
> unset. Needs its own fix in `ConvertSPIRVToLLVMPass.cpp` (unconfirmed exact
> shape: possibly merging both entry points' tessellation execution-mode fields
> before applying attributes, since a hull/domain pair is always compiled and
> linked together and the full tessellation state is genuinely spread across
> both entries in this real DXC output, not just one) -- and, since a genuinely
> malformed input (a domain shader truly missing *all* tessellation state, from
> either entry) should still be rejected, needs its own new unit test confirming
> that diagnostic still fires correctly once the fix lets the real, valid,
> split-across-both-entries shape through. Not yet started.
