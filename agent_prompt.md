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

Can you work on milestone H5d-a?

> **`GeometryState` has no `Invocations` field, and `Executor.cpp`'s new
> geometry-chaining block (H5d) always invokes a bound geometry stage exactly
> once per input primitive** -- correct for GLSL's default `layout(invocations =
> 1)`, but not for a real shader that declares `layout(invocations = N)` for `N
> > 1`, which SPIR-V/GLSL geometry shaders can and do use to invoke the same
> entry point `N` times per primitive, each with a distinct `gl_InvocationID`
> (`SystemValue::InvocationID`, already representable in a `Signature` per
> `getSystemValueForBuiltIn`, but never populated into a real invocation record
> by H5d's own `FemeGeometryInvocation`-building loop, which has no
> per-invocation-index dimension at all today). Needs: a
> `GeometryState::Invocations` field (mirroring `TessellationState`'s own
> shape), `Executor.cpp`'s invocation-building loop widened to build
> `Invocations * PrimitiveCount` records instead of `PrimitiveCount`, each
> stamped with its own `gl_InvocationID`, and
> `collectGeometryStreams`/`mergeGeometryStreamsInLaneOrder`'s own lane-ordering
> contract re-checked against multiple invocations per primitive (today one
> invocation is one lane; N invocations per primitive means N lanes per
> primitive, which the merge's existing "lane order" concept may already
> tolerate, needs confirming with a real test rather than assumed)
