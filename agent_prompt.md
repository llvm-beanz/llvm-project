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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement milestone R34 from the roadmap document?

> Geometry/hull/domain signatures and wrappers, patch storage, control-stage
> barriers, tessellator state and domain-coordinate generation, bounded geometry
> streams, stream output, adjacency, layered rendering (status: the host-side,
> standalone-tested core lands -- the signature/stage-op model
> (`SignatureSystemValue::TessFactorEdge`/`TessFactorInside`/`DomainLocation`/`OutputControlPointID`,
> `StageOpKind::StreamEmit`/`StreamCut`, patch input/output reusing the existing
> `InputLoad`/`OutputStore` ops), the fixed-function tessellator
> (`feme::graphics::tessellate`, new Tessellator.h, generating domain
> coordinates/connectivity for isoline/triangle/quad domains across every
> partitioning/output-primitive combination, including crack-free non-uniform
> per-edge tessellation for the triangle/quad domains -- each edge's own factor
> places that edge's boundary vertices, so two adjacent patches agreeing on a
> shared edge's factor produce identical vertices along it regardless of their
> other factors, bridged to a uniformly-subdivided interior core via a
> concentric-ring triangulation, `bridgeRingsByEdge`), bounded patch storage
> (`feme::graphics::PatchRecord`, new Patch.h -- control-stage barriers need no
> new code, since `feme::cpu`'s groupshared/barrier lowering is already
> stage-agnostic), the four adjacency `PrimitiveTopology` variants plus list-
> and strip-topology adjacency splitting (Pipeline.h's
> `splitListPrimitiveAdjacency`/`splitStripPrimitiveAdjacency`), a bounded
> per-invocation multi-stream geometry builder
> (`feme::graphics::GeometryStreamBuilder`, new GeometryStream.h) retaining
> strip boundaries/emission order for stream output and rasterization to share,
> plus `mergeGeometryStreamsInLaneOrder` (added after R34's initial landing to
> close its own "documented follow-up"): SIMD-lane stream-range reservation via
> a checked prefix sum, merging one per-lane builder into a combined one in
> deterministic lane order, rejecting a lane's (and every later lane's, for that
> stream) whole reservation rather than overflowing the combined builder's
> declared capacity, and forcing a strip boundary at every lane edge even when a
> lane's own trailing strip was left open; layered-rendering array-layer
> selection that discards rather than clamps an out-of-range index
> (`feme::graphics::resolveRenderTargetArrayLayer`, new LayeredRendering.h, plus
> `AttachmentView::ArrayLayers`); and, added after R34's initial landing to
> begin closing its largest deferred item, `feme::cpu::HullWrapperPass` (new
> HullWrapper.h/.cpp) plus
> `FemePatchArgs`/`PreparedPatchBatch`/`CompiledStage::invokePatch`: the
> control-point phase of a real hull entry point, compiled through the CPU
> lowering pipeline into an invokable batch, for the common
> per-control-point-independent shape (each control point reads only its own
> input control point's attributes, addressed by
> `StageLayoutSystemValue::OutputControlPointID`) -- see HullWrapper.cpp's file
> comment for why this phase alone needs none of `EntryWrapperPass`'s
> barrier-region-splitting machinery (the patch-constant function is a separate
> compiled entry receiving the *completed* `OutputPatch`, so the phase boundary
> itself is the synchronization point) and what two shapes remain diagnosed
> rather than silently mishandled (a control point indexing a sibling control
> point's input, and a group-sync barrier within the phase); and, added in a
> further follow-up session to close that same deferred list's first item,
> `feme::cpu::PatchConstantWrapperPass` (new PatchConstantWrapper.h/.cpp) plus
> `FemePatchConstantArgs`/`PatchConstantResources`/`PreparedPatchConstantBatch`/`CompiledStage::invokePatchConstant`:
> the hull shader's second phase, a single non-batched invocation per patch that
> may read any (not only its own) output control point of the completed
> `OutputPatch` and writes tessellation factors/patch constants to unbatched
> per-patch storage, still routed through the general SIMDize/WaveLowering
> machinery but invoked with only lane 0 active rather than a wave loop over
> some batch count. `feme::cpu::isPatchConstantPhase` (new HullPhase.h/.cpp,
> private to `lib/Transforms/CPU`) discriminates a hull-stage function's two
> phases -- Direct3D/Vulkan give the patch-constant function no stage of its own
> -- by checking for a `SignatureDirection::PatchOutput` element, which only the
> patch-constant phase ever writes; `HullWrapperPass` now skips a candidate this
> identifies as the patch-constant phase. And, added in a further follow-up
> session to close that deferred list's remaining, "smaller, more scoped" item,
> an `InputPatch` parameter on the patch-constant function (the original,
> pre-control-stage input control points, distinct from the completed
> `OutputPatch`): `FemePatchConstantArgs` grows a second, independent
> structure-of-arrays input block
> (`InputPatch`/`InputPatchLayout`/`InputPatchControlPointCount`), and
> `SignatureElement::FromInputPatch` on a `SignatureDirection::Input` element
> tells `PatchConstantWrapperPass`'s `lowerPatchConstantInputLoad` which of the
> two blocks a given `feme.stage.input.load` addresses. And, added in a further
> follow-up session to close that deferred list's own largest remaining item,
> `feme::cpu::DomainWrapperPass` (new DomainWrapper.h/.cpp) plus
> `FemeDomainInvocation`/`FemeDomainArgs`/`DomainResources`/`PreparedDomainBatch`/`CompiledStage::invokeDomain`:
> the domain (evaluation) stage, batched one independent invocation per
> tessellator-generated domain point exactly the way `VertexWrapperPass` batches
> vertices, with each `feme.stage.input.load` routed by the signature element it
> names to one of this stage's three input sources -- the completed patch's
> control points (`SignatureDirection::Input`, readable at any control-point
> index, since evaluating a patch means blending its control points), the
> per-patch tessellation factors/patch constants
> (`SignatureDirection::PatchInput`, addressed by row/component alone, the
> mirror image of the patch-constant phase's own unbatched output store), and
> `SV_DomainLocation`
> (`SignatureSystemValue::DomainLocation`/`StageLayoutSystemValue::DomainLocation`,
> read from the per-invocation `FemeDomainInvocation` record the way a vertex
> batch reads `SV_VertexID` from its own) -- writing ordinary per-vertex
> outputs, since a domain shader's result is a vertex; a dynamically indexed
> domain-location component (the record is a fixed-size ABI struct) and a
> group-sync barrier (domain invocations are independent) are diagnosed rather
> than silently mishandled. And, added in a further follow-up session to close
> that deferred list's last remaining "wrapper" item,
> `feme::cpu::GeometryWrapperPass` (new GeometryWrapper.h/.cpp) plus
> `FemeGeometryInvocation`/`FemeGeometryArgs`/`GeometryResources`/`PreparedGeometryBatch`/`CompiledStage::invokeGeometry`:
> one invocation per assembled input primitive, batched over
> `FemeGeometryArgs::PrimitiveCount` exactly the way `VertexWrapperPass` batches
> vertices, reading a structure-of-arrays input block addressed `primitive *
> VerticesPerPrimitive + vertexInPrimitive` (any vertex in the primitive, not
> just the invocation's own, unlike the hull control-point phase's own
> restriction -- an adjacency triangle's "opposite" vertices, for instance).
> `feme.stage.stream.emit`/`.cut` (`StageOpKind::StreamEmit`/`StreamCut`) turn
> ordinary per-invocation output-store scratch storage into the stage's real,
> bounded, variable-count result: `emit` snapshots that scratch storage into one
> record of three flat, host-owned arrays rather than calling back into a live
> `GeometryStreamBuilder` object from JIT-compiled code (no precedent in this
> codebase for that), and `feme::graphics::collectGeometryStreams` (new
> GeometryStreamCollection.h/.cpp, living in `feme::graphics` since
> `FeMeTargetCPU` does not depend on `FeMeGraphics`) replays those flat records
> back into one real `GeometryStreamBuilder` per primitive and merges them via
> `mergeGeometryStreamsInLaneOrder`, finally closing that function's own
> "driving it from a real widened invocation" deferral. This also closed a
> latent gap: `feme.stage.stream.emit`/`.cut` needed the same per-lane
> side-effect-mask threading `feme.stage.output.store` already had
> (`LinearizePass` now creates masked variants of them, and `FunctionWidener`
> widens those variants in SIMDize.cpp), since without it SIMDize left a
> uniform-operand `feme.stage.stream.emit`/`.cut` call completely unwidened,
> firing once per *wave* rather than once per active *lane*. Two shapes remain
> diagnosed rather than silently mishandled: more than one output stream (this
> milestone's `FemeGeometryArgs` only carries storage for stream 0), and a
> group-sync barrier (geometry invocations are independent, like the domain
> stage's). (Crack-free non-uniform per-edge tessellation, previously deferred
> here, was added after R34's initial landing -- see `bridgeRingsByEdge` in
> Tessellator.cpp.) And, added in a further follow-up session to begin closing
> the "wiring the compiled hull/domain/geometry stages into
> `executeDraws`/`feme-render`" item (the last of the two items the prior
> session's open-issue list carried), three pieces of host-side marshaling glue,
> each unit-tested standalone ahead of a real chained draw exercising them
> together: `feme::graphics::PatchRecord` (Patch.h) grows a second, independent
> structure-of-arrays block for a patch's original input control points
> (`writeInputControlPoint`/`readInputControlPoint`, alongside the existing
> output-control-point storage), which `PatchConstantWrapperPass`'s `InputPatch`
> parameter needs a per-patch home for; `feme::graphics::buildDomainInvocations`
> (new DomainInvocations.h/.cpp) converts a `feme::graphics::tessellate`
> result's `DomainPoint`s into a `feme::cpu::FemeDomainInvocation` array for
> `FemeDomainArgs::Invocations`; and
> `feme::graphics::buildGeometryInputs`/`buildGeometryInvocations` (new
> GeometryInputs.h/.cpp) gather an assembled primitive batch's vertex-stage
> output attributes into `FemeGeometryArgs::Inputs`'s primitive-major layout
> (given the same per-primitive vertex-index lists
> `splitListPrimitiveAdjacency`/`splitStripPrimitiveAdjacency` already produce)
> and build one `FemeGeometryInvocation` per primitive's `SV_PrimitiveID`. Still
> deferred, documented in HullWrapper.cpp's and GeometryWrapper.cpp's own
> comments and in this row's own history above: generalizing
> `EntryWrapperPass`'s barrier-region splitting to the control-point batch ABI
> for a hull shader that needs it, and actually chaining the four compiled stage
> invocations (hull control-point phase, patch-constant phase, domain, geometry)
> together per patch/primitive and wiring the result into
> `executeDraws`/`feme-render` -- this session's glue narrows that gap but does
> not close it: `feme::graphics::Executor` does not yet call
> `invokePatch`/`invokePatchConstant`/`invokeDomain`/`invokeGeometry` at all.
> `unittests/Graphics/{Tessellator,Patch,GeometryStream,GeometryStreamCollection,LayeredRendering,DomainInvocations,GeometryInputs}Test.cpp`,
> `unittests/Transforms/CPU/{HullWrapper,PatchConstantWrapper,DomainWrapper,GeometryWrapper}Test.cpp`
> (including `LowersInputPatchAndOutputPatchReadsSeparately` and
> `LowersAllThreeInputSourcesAndBuildsWrapper`),
> `unittests/Target/CPU/CompiledStageTest.cpp`'s
> `InvokePatch{,Constant}RunsStageAwarePath`,
> `InvokePatchConstantReadsInputPatchSeparatelyFromOutputPatch`,
> `InvokeDomainRunsStageAwarePath` and `InvokeGeometryRunsStageAwarePath` cases,
> and `PipelineTest.cpp`'s/`SignatureTest.cpp`'s/`StageOpsTest.cpp`'s new cases
> cover today's scope; `ninja check-feme` (assertions-enabled, ccache build)
> passes in full before and after -- G5 is not yet complete, since no
> image-comparison completion test exists)
