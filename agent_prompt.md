---
model: claude-sonnet-5
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
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on milestone H5d?

> **Chain the geometry stage into `Executor::executeDraws`**, the "as does the
> whole ... executor" half FeMeGraphicsDesign.md's "Tessellation and geometry
> stage model" section already flags open: assemble each draw's primitives
> (`splitListPrimitiveAdjacency`/`splitStripPrimitiveAdjacency`, Pipeline.h,
> already implemented), gather their vertex attributes into a geometry batch
> (`feme::graphics::buildGeometryInputs`/`buildGeometryInvocations`,
> GeometryInputs.h, already implemented), run the compiled geometry stage
> (`CompiledStage::invokeGeometry`, already implemented), replay its flat
> emitted-vertex/strip-boundary records back into one `GeometryStreamBuilder`
> per primitive and merge them in lane order
> (`feme::cpu::collectGeometryStreams`/`feme::graphics::mergeGeometryStreamsInLaneOrder`,
> both already implemented), and rasterize the merged stream's own strips in
> place of the vertex/domain stage's output -- the same "last pre-rasterization
> stage" substitution H4's own tessellation chaining already established the
> pattern for (`RasterSig`). `GraphicsPipeline` needs a
> `setGeometryStage`/`hasGeometryStages` pair mirroring
> `setTessellationStages`/`hasTessellationStages`. Needs real unit coverage in
> `ExecutorTest.cpp` (a hand-compiled trivial geometry stage rendering,
> mirroring the tessellation domain-stage rasterization tests) before H5e's real
> SPIR-V-sourced pipelines can be trusted to hit the same code path correctly
