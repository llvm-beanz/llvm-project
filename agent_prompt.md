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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you continue the R34 implementation from the roadmap document?

> Geometry/hull/domain signatures and wrappers, patch storage, control-stage
> barriers, tessellator state and domain-coordinate generation, bounded geometry
> streams, stream output, adjacency, layered rendering (status: the host-side,
> standalone-tested core lands -- the signature/stage-op model
> (`SignatureSystemValue::TessFactorEdge`/`TessFactorInside`/`DomainLocation`/`OutputControlPointID`,
> `StageOpKind::StreamEmit`/`StreamCut`, patch input/output reusing the existing
> `InputLoad`/`OutputStore` ops), the fixed-function tessellator
> (`feme::graphics::tessellate`, new Tessellator.h, generating domain
> coordinates/connectivity for isoline/triangle/quad domains across every
> partitioning/output-primitive combination; triangle/quad interiors subdivide
> uniformly from the largest/inside factor rather than placing per-edge boundary
> vertices and stitching a crack-free fan, a documented scope note in its own
> file comment), bounded patch storage (`feme::graphics::PatchRecord`, new
> Patch.h -- control-stage barriers need no new code, since `feme::cpu`'s
> groupshared/barrier lowering is already stage-agnostic), the four adjacency
> `PrimitiveTopology` variants plus list- and strip-topology adjacency splitting
> (Pipeline.h's `splitListPrimitiveAdjacency`/`splitStripPrimitiveAdjacency`), a
> bounded per-invocation multi-stream geometry builder
> (`feme::graphics::GeometryStreamBuilder`, new GeometryStream.h) retaining
> strip boundaries/emission order for stream output and rasterization to share,
> and layered-rendering array-layer selection that discards rather than clamps
> an out-of-range index (`feme::graphics::resolveRenderTargetArrayLayer`, new
> LayeredRendering.h, plus `AttachmentView::ArrayLayers`). Deferred, each
> documented in its own file's comment: compiling a real hull/domain/geometry
> entry point through the CPU lowering pipeline into an invokable
> `CompiledStage` batch (neither stage has a
> `VertexWrapperPass`/`FragmentWrapperPass` counterpart yet) and wiring the
> result into `executeDraws`/`feme-render`; SIMD-lane stream-range reservation
> via checked prefix sums; and crack-free non-uniform per-edge tessellation.
> `unittests/Graphics/{Tessellator,Patch,GeometryStream,LayeredRendering}Test.cpp`
> and `PipelineTest.cpp`'s/`SignatureTest.cpp`'s/`StageOpsTest.cpp`'s new cases
> cover today's scope; `ninja check-feme` (assertions-enabled, ccache build)
> passes in full before and after -- G5 is not yet complete, since no
> image-comparison completion test exists) (see: §1.8.5)

Open issues form the last agent task:

> 1. The patch-constant function: no ABI struct or wrapper pass yet. It needs
>    its own shape (`OutputPatch`/`InputPatch` inputs, a single
>    non-batched/scalar invocation per patch rather than a wave loop, tess
>    factor + patch constant outputs feeding `feme::graphics::PatchRecord`).
> 2. `DomainWrapperPass`/`GeometryWrapperPass` and
>    `CompiledStage::invokeDomain`/`invokeGeometry` -- not started.
> 3. Generalizing `EntryWrapperPass`'s barrier-region-splitting machinery to
>    the control-point batch ABI, for a hull shader whose control points
>    cooperate through groupshared memory before every one finishes (today
>    diagnosed by `HullWrapperPass` rather than supported). Still, as the
>    prior two sessions found for the whole wrapper item, "its own
>    multi-commit body of work" -- but now scoped to exactly this one
>    narrower case rather than the whole hull/domain/geometry wrapper
>    surface.
> 4. Wiring any compiled hull stage into `executeDraws`/`feme-render`/the
>    scene YAML -- still strictly downstream of the above, and of the domain
>    wrapper (a control-point batch alone produces no rasterizable geometry
>    without going through the tessellator and a domain-stage evaluation).
