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

Can you work on milestone H5b?

> **Thread a non-constant, dynamically-indexed SPIR-V array index into
> `feme.stage.input.load`'s `Vertex` operand**, the gap H5a's own investigation
> found: a geometry entry point's per-vertex inputs (`gl_in[]`-shaped) are read
> via `gl_in[i]` for a loop-carried `i`, but `loadStageIOValue`'s existing
> recursion always passes a constant `Zero` for `Vertex` -- there is no code
> path today that recognizes a SPIR-V array-typed `Input`-storage-class global
> being indexed by a real SSA value (as opposed to the constant-offset-only
> `getStageIOBaseAndOffset`/`resolveStageIOAccess` byte-offset resolution H2d
> built for a builtin interface block's own *member* access) and threads that
> index through as `Vertex` instead of `Row`/`Component`. Needs: recognizing an
> array-of-per-vertex-block SPIR-V `Input` global (distinct from a matrix's
> `Row` dimension, which is a compile-time-fixed shape, not a genuine "the
> pipeline supplies N of these, addressed by this stage's own bounded loop
> variable" one); extracting the outer (vertex) index as a `Value*` rather than
> requiring it constant; and validating (`ValidateStagePass`, itself still not
> extended to Geometry -- see H5e) that a non-constant `Vertex` operand is only
> ever legal for a stage whose ABI actually supports it (`FemeGeometryArgs`'s
> primitive-major `Inputs` layout), diagnosed otherwise.
> `CanonicalizeStagePass::run` must not accept `ShaderStage::Geometry` until
> this lands (H5a's own report explains why). Whole `dEQP-VK.geometry` group
> remains blocked on this
