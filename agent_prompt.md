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
agent_thoughts.md file.

# Request

Can you work on H93b or other blocking work to make progress on the H-series
milestones?

> **Thread an explicit mesh (or geometry) shader-authored `gl_PrimitiveID`
> through to the fragment invocation, instead of the rasterizer always
> overwriting it with an auto-incrementing raster-order counter**:
> `Executor.cpp`'s triangle/line/point-emitting lambda (~`ST.PrimitiveID =
> PrimitiveCounter++`) unconditionally synthesizes every fragment invocation's
> `gl_PrimitiveID` from raster order -- correct only as the Vulkan-spec
> *fallback* for when no earlier stage writes it, but applied even when a
> mesh/GS stage explicitly does. `StageStorage.cpp` (~line 95-135) deliberately
> excludes any `SystemValue`-tagged `Input` element (except
> `ClipDistance`/`CullDistance` and geometry-input vertex-array members) from
> ordinary interpolated stage storage, so there is currently no path at all for
> an authored primitive-output `PrimitiveID` to reach the fragment side. For
> `max_mesh_output_primitives_256` specifically, `emitPointQuad` calls the
> triangle-emitting lambda twice per point (a point becomes a 2-triangle quad),
> so `PrimitiveCounter` advances 2 per point; assuming (plausibly, not yet
> re-confirmed with a direct `Inv.PrimitiveID` print after H93a landed) the
> first-pushed triangle of each quad always wins the CTS's 1x1-pixel
> framebuffer, point `P`'s surviving fragment gets auto-ID `2*P` -- exactly
> matching the observed symptom (all 128 even indices `0,2,...,254` of the CTS's
> `ssbo.flags` get set, all 128 odd ones never do, and `P>=128` produces
> silently-dropped out-of-bounds writes). Needs: (1) a way for
> `EntrySignature`/stage linking to recognize when a producing stage's `Output`
> elements include a `SystemValue::PrimitiveID` and thread that fact to the
> rasterizer (likely via `StageLink.cpp`); (2) `Executor.cpp`'s
> fragment-invocation assembly to prefer that authored value (sourced from
> `Merged`/`PrimitiveOutputs`, by primitive index, not raster order) over
> `PrimitiveCounter++` whenever present, for every primitive class
> (points/lines/triangles alike, not just the mesh-point shape that exposed it);
> (3) regression coverage at both the `Executor.cpp` unit-test level (an
> explicit-`PrimitiveID`-authoring mesh entry, asserting the fragment
> invocation's `PrimitiveID` matches the authored value rather than raster
> order) and a real CTS re-run of `max_mesh_output_primitives_256` confirming it
> passes outright. Not yet started -- deferred here as its own milestone given
> the architectural scope (touches `StageStorage.cpp`, `StageLink.cpp`, and
> `Executor.cpp`'s triangle/line/point assembly all at once) rather than
> attempted as a quick follow-on patch
