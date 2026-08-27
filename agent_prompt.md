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

Can you work on milestone H4d?

> **A JIT-time `"Symbols not found: [ spirv_var_N, spirv_var_N ]"` error rejects
> some tessellation-control shaders that do *not* hit H4c's SSA-capture
> diagnostic** (24 of H4b's own measured 227 `dEQP-VK.tessellation.*` `Fail`s,
> e.g. all of `dEQP-VK.tessellation.winding.*`): the control-point and
> patch-constant phases each get their own `CompiledStage::create`/`LLJIT`
> (H4b's own design, matching every other stage), so this is not a cross-phase
> symbol collision; more likely `splitTessellationControlEntry`'s clone of the
> module for one phase leaves a global variable referenced-but-undefined after
> some later pass (a `GlobalDCE`-shaped pass reachable only from the phase's own
> new entry point, not the original one) strips its definition while a reference
> to it survives. Root cause not yet isolated -- needs its own investigation,
> likely starting from a minimal reproduction of
> `dEQP-VK.tessellation.winding.default_domain.glsl_quads_ccw`'s own
> tessellation-control module reduced through the same
> `feme-convert-spirv-to-llvm`/`feme-canonicalize-stage` pipeline H4b's own
> tests used to hand-verify shapes
