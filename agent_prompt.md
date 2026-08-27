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

Can you work on milestone H5f?

> **A *constant*-indexed `gl_in[k]`-shaped access (or any other
> per-vertex-arrayed `Input` global) still resolves through the pre-existing
> `getStageIOBaseAndOffset`/`resolveRowComponent` byte-offset path exactly as
> before H5b, folding that constant array index into the accessed member's own
> `Row` operand instead of `Vertex`**, and the corresponding
> `SignatureElement.RowCount` for such a global still reports the per-vertex
> array's own extent (e.g. 3 for a triangle's `gl_in[3]`) rather than a real
> matrix's row count -- the two are indistinguishable in the signature today,
> which H5b's own investigation found and deliberately left alone (out of that
> row's own scope, which asked only for the non-constant case). Needs a way for
> a `SignatureElement` (or a sibling flag alongside it) to record "this
> dimension is a per-vertex array, not a matrix row" so a real consumer (H5c's
> own geometry-entry canonicalization, or a future one) can tell the two apart
> regardless of whether the shader's own index into it happens to be constant,
> and route a constant index through `Vertex` (not `Row`) for consistency with
> the non-constant case H5b already handles
