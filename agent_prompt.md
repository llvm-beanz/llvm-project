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

Can you complete and close out milestone H6h?

> **Give `TaskPayloadWorkgroupEXT` an address-space convention and a
> global-variable import pattern**: LLVM's own SPIR-V backend
> (`storageClassToAddressSpace` in `llvm/lib/Target/SPIRV/SPIRVUtils.h`) has no
> mapping at all for `TaskPayloadWorkgroupEXT` (SPIR-V enum 5402) -- unlike
> `Input`(7)/`Output`(8)/`Workgroup`(3)/`PushConstant`(13), which FeMe's own
> `StageIOGlobalVariablePattern`/`WorkgroupGlobalVariablePattern`/`PushConstantGlobalVariablePattern`
> (`feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`) already reuse from
> that fixed mapping -- so a task entry's payload variable cannot be imported as
> an LLVM global at all today (found during H6b's own investigation). Needs a
> new address space (any value FeMe's own conversion layer does not otherwise
> use) and a `TaskPayloadGlobalVariablePattern` mirroring the two precedents
> above, before `CanonicalizeStage.cpp` has anything to canonicalize a payload
> write into
