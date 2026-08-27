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

Can you work on milestone H4f?

> **`splitTessellationControlEntry` (H4a/H4c) only clones a
> `<entry>.patchconstant` phase when it finds a barrier -- a
> tessellation-control shader with none (legally the case whenever
> `OutputVertices == 1`, since a single control-point invocation needs no
> cross-invocation synchronization, `dEQP-VK.tessellation.winding.*`'s own
> shape) leaves `PatchConstantPhase == nullptr`, but `compileAndValidateStages`
> (H4b) unconditionally expects a `.patchconstant` sibling to exist and compiles
> it as a second stage regardless** (24 of H4d's own re-measured
> `dEQP-VK.tessellation.*` `Fail`s, all of `dEQP-VK.tessellation.winding.*`'s
> glsl variants -- confirmed via a real `deqp-vk` run, root-caused and isolated
> as part of H4d's own investigation but deliberately not fixed there, since it
> is a distinct gap in the barrier-split design itself, not the
> `resolveStageIOAccess` array-addressing bug H4d fixed). The whole entry, in
> this no-barrier shape, is semantically already "the patch-constant phase" for
> `OutputVertices == 1` (there is only one control point, so nothing
> meaningfully distinguishes "per control point" from "per patch" here) -- needs
> either (a) treating a no-barrier entry as *solely* a patch-constant phase
> whenever every one of its stage-IO writes is patch-frequency
> (`Patch`-decorated or a tess-factor `BuiltIn`), with an empty/trivial
> control-point phase synthesized instead, or (b) the more general fix of
> cloning the *whole* function as `<entry>.patchconstant` unconditionally when
> there is no barrier (sound only when `OutputVertices == 1`, since with more
> than one output control point and no barrier there is by definition no legal
> way for one invocation to see another's data, so nothing but the current
> invocation's own patch-frequency writes could safely be duplicated this way
> either -- needs care not to naively assume general soundness)
