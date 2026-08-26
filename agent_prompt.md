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

Can you work on milestone H2d?

> **Decompose a builtin interface block into one `SignatureElement` per member
> during `CanonicalizeStagePass` legalization**, now that H2c makes each
> member's own decorations available: `isSPIRVStageIOGlobal`'s "one global, one
> signature element" assumption (`CanonicalizeStage.cpp`) needs to become "one
> global, N signature elements, one per struct member", each keeping its own
> `BuiltIn`/system-value identity (`gl_Position` ->
> `SignatureSystemValue::Position`,
> `gl_PointSize`/`gl_ClipDistance`/`gl_CullDistance` -> `None`, unmodeled system
> values, matching how an unrecognized DXIL semantic already converts), and
> `loadStageIOValue`/`storeStageIOValue`'s existing recursive per-(struct
> member, row, component) decomposition (built for C8a's matrix/aggregate case)
> needs to route each member through its own `ElementID` instead of the whole
> block's single one. `isSPIRVStageIOGlobal` also needs to recognize a global
> carrying only `feme.spirv.MemberDecorations` (no whole-variable
> `!spirv.Decorations`), the shape H2c's own builtin-interface-block global now
> produces. Closes the whole `dEQP-VK.multiview` group's own remaining largest
> blocker
