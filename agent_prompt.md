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

Can you implement roadmap milestone F15d?

> **`VK_KHR_shader_float_controls2`'s `FPFastMathDefault` execution mode, and
> the missing `FloatControls2` capability** (F15c's own remaining half): a
> targeted CTS run
> (`dEQP-VK.spirv_assembly.instruction.compute.float_controls2.*`, see
> `VulkanCTSReport.md`) found a second, more fundamental gap alongside
> `FPFastMathDefault` itself: MLIR's `spirv` dialect has no `FloatControls2`
> capability enumerant at all (SPIR-V capability 6029; absent from
> `SPIRVBase.td`'s `SPIRV_C_*` list), so a real CTS-generated shader declaring
> `OpCapability FloatControls2` -- which every shader using this extension's own
> decorations does -- fails to deserialize at all ("unknown capability: 6029"),
> regardless of how correct F15c's own decoration handling is. This row needs,
> in order: (1) the `FloatControls2` capability enumerant itself, so real
> shaders using this extension can be imported in the first place; (2) the
> `FPFastMathDefault` execution-mode enumerant (also absent from
> `SPIRVBase.td`'s `SPIRV_EM_*` list), which sets a per-floating-point-type
> default `FPFastMathMode` for every otherwise-undecorated instruction in an
> entry point, via `OpExecutionModeId` naming a spec constant; (3) the newer,
> non-INTEL `AllowContract`/`AllowReassoc`/`AllowTransform` `FPFastMathMode`
> bits the extension itself adds (distinct from this dialect's existing
> `AllowContractFastINTEL`/`AllowReassocINTEL` vendor pair, which F15c's own
> `FPFastMathMode` decoration support already maps); (4)
> `FloatControlArithmeticPattern`/collectEntryPoints
> (SPIRVToLLVMPatterns.cpp/ConvertSPIRVToLLVMPass.cpp) to apply an entry point's
> declared default, per floating-point type, to every arithmetic op of that type
> lacking its own `FPFastMathMode` decoration -- the same "decoration overrides
> entry-point-wide default" precedence F15c's own `FPRoundingMode` override
> already established for `RoundingModeRTZ`. (1)-(3) are dialect-level gaps
> outside `feme/lib/Conversion/SPIRVToLLVM`'s own layering to fix
