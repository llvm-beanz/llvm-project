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

Can you work on milestone H4h?

> **Fixing H4f and H4g still does not turn any of
> `dEQP-VK.tessellation.winding.*`'s glsl cases green -- all 24 now fail at a
> third, later blocker: `validateStageInterfaces` (`GraphicsPipeline.cpp`)
> unconditionally requires the *vertex* stage to write a 4-component
> `SV_Position`/`gl_Position` output** (`findSystemValue(*VSSig,
> SignatureDirection::Output, SignatureSystemValue::Position)`), which is
> correct for the ordinary vertex -> fragment pipeline (the rasterizer needs a
> clip-space position from *somewhere*, and without a domain stage the vertex
> stage is the only producer) but is not a real requirement once a tessellation
> evaluation stage exists: the CTS's own `winding` test's vertex shader is
> deliberately and legally empty (`void main (void) {}`; confirmed by reading
> `vktTessellationWindingTests.cpp` directly), because its tessellation
> evaluation shader computes `gl_Position` purely from `gl_TessCoord`
> (`gl_Position = vec4(gl_TessCoord.xy*2.0 - 1.0, 0.0, 1.0);`) and never reads
> back any vertex-stage output via `gl_in[]`. `validateStageInterfaces` needs to
> skip (or relax) its `SV_Position` requirement on the vertex stage specifically
> when a tessellation evaluation stage is present in the pipeline, since in that
> shape the *domain* stage's own output is what gets rasterized (per
> `PatchPipeline.cpp`'s `runPatchPipeline`/`Executor.cpp`), not the vertex
> stage's -- root-caused and isolated as part of H4f/H4g's own re-measurement
> but deliberately not fixed here, since it is a distinct validation-layer gap,
> not a stage-splitting or signature-serialization one. Confirmed via a real
> `deqp-vk` run with `FEME_VULKAN_LOG_CREATION_ERRORS=1`: all 24
> `dEQP-VK.tessellation.winding.*` glsl cases now fail with exactly `"vertex
> stage does not write a 4-component SV_Position output"` (0 occurrences of
> either of H4f/H4g's own former blockers)
