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

Can you work on milestone H4a? The last agent stopped part way through without
committing its progress. I've stashed its changes, you can restore them with
`git stash pop`.

> **No SPIR-V tessellation-control or tessellation-evaluation entry point can be
> reflected, so H4b has nothing to compile.**
> `feme::graphics::CanonicalizeStagePass::run`
> (`feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`, ~line 1200) skips every
> function whose `feme::getShaderStage` is not `Vertex` or `Fragment`, so a
> `TessellationControl`/`TessellationEvaluation` entry point reaches code
> generation with no `EntrySignature` attached and cannot be wrapped. Lifting
> that filter is necessary but nowhere near sufficient: FeMe's hull ABI is
> D3D-shaped and needs **two** separately compiled entry points -- a
> control-point phase for `HullWrapperPass` and a patch-constant phase for
> `PatchConstantWrapperPass`, discriminated by
> `feme::cpu::isPatchConstantPhase`'s `SignatureDirection::PatchOutput` test --
> whereas GLSL/SPIR-V compiles a tessellation-control shader to a **single**
> entry point that writes both its per-vertex outputs and
> `gl_TessLevelOuter`/`gl_TessLevelInner`, typically with an intervening
> `OpControlBarrier`. Splitting that one entry into the two FeMe phases is
> exactly R34's own still-deferred "generalize `EntryWrapperPass`'s
> barrier-region splitting to the control-point batch ABI" item, and is the real
> work here. Also needs SPIR-V's tessellation execution modes
> (`Triangles`/`Quads`/`Isolines`,
> `SpacingEqual`/`SpacingFractionalEven`/`SpacingFractionalOdd`,
> `VertexOrderCw`/`VertexOrderCcw`, `PointMode`, `OutputVertices`) mapped onto
> `feme::graphics::TessellationState`, and `BuiltIn`
> `TessLevelOuter`/`TessLevelInner`/`TessCoord`/`PatchVertices`/`InvocationId`
> mapped onto the corresponding `SignatureSystemValue`s
