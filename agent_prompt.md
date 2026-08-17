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

> 1. Generalizing `EntryWrapperPass`'s barrier-region-splitting machinery to
>    the control-point batch ABI, for a hull shader whose control points
>    cooperate through groupshared memory before every one finishes. Unchanged
>    from every prior session's list.
> 2. Wiring the compiled hull, domain, and (now) geometry stages into
>    `executeDraws`/`feme-render`/the scene YAML: this is now the *only*
>    remaining item blocking G5's image-comparison completion test, since all
>    four wrapper passes exist. It still needs host-side glue that does not
>    exist: `feme::graphics::PatchRecord` has no storage for the original
>    input control points, nothing marshals a tessellator's `DomainPoint`
>    output into a `FemeDomainInvocation` array or a primitive's assembled
>    vertices into a `FemeGeometryInvocation`/`Inputs` block, and nothing
>    chains the four stage invocations (hull control-point phase,
>    patch-constant phase, domain, geometry) together per patch/primitive.
