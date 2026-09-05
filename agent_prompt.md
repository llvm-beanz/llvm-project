---
model: claude-sonnet-5
resume: ec2f5570-263a-4b95-917f-6c2230e594cf
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
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L38 or other prerequisites blocking the L-series milestones?

> **A pre-existing, GLSL-path (not HLSL/DXC) crash discovered while measuring
> L29's own CTS impact**:
> `dEQP-VK.glsl.matrix.add.const.highp_mat2_float_fragment` (and, unconfirmed,
> likely every other case under the 1,764-case `dEQP-VK.glsl.matrix.*.const.*`
> groups) aborts with `"error: FloatAttr does not match expected type of the
> constant"` immediately followed by an `llvm::dyn_cast` assertion
> (`Casting.h:656`, "dyn_cast on a non-existent value") -- confirmed
> pre-existing and unrelated to L29's own fix via a stash/rebuild bisection
> reproducing the identical crash before that row's change landed. Likely a
> distinct SPIR-V-constant-conversion gap in a different pattern than
> `ArrayConstantPattern` (glslang's own `OpConstantComposite`/`OpSpecConstant*`
> lowering for a scalar or per-component GLSL matrix constant probably takes a
> different MLIR conversion path than DXC's matrix constants do), needing its
> own real IR reduction (glslangValidator or `dxc`-equivalent on a minimal GLSL
> fragment shader adding a `const mat2` to a `float`) to identify which
> conversion pattern emits a `FloatAttr` whose type upstream's constant-building
> code doesn't expect
