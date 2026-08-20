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
after each change and update the VulkanCTSReport.md.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement C8 from the roadmap document?

> **Shader long tail.** Descriptor arrays of combined image samplers (816),
> matrix/aggregate stage IO (309), the SPIR-V importer's `unhandled opcode` set
> (171), `Workgroup` arrays-of-arrays (151), the 277 individually-unlegalized
> ops (`spirv.SpecConstant`, `spirv.VectorExtractDynamic`,
> `spirv.CompositeConstruct`, the `spirv.Atomic*` family, ...), and the 242-case
> diagnostic tail. Best attacked *after* C2/C3, since the true size of this
> bucket is unknown until the stacked blockers ahead of it are gone. **C3's own
> measurement found a new member of this bucket, larger than any row already in
> it**: a plain, non-atomic `load`/`store` on a raw SPIR-V
> `Input`/`Output`-storage-class global (address space 7/8, see
> `getStageIOAddressSpace` in SPIRVToLLVMPatterns.cpp) is never canonicalized
> into the `feme.stage.*` calls `feme::cpu::LinearizePass`/`SIMDizePass` already
> know how to widen the way a DXIL/HLSL-imported shader's stage IO always is
> (`feme::dxil::OpRaisingPass`) -- so a SPIR-V-imported fragment/vertex shader's
> own divergent output store hits `feme-cpu-simdize`'s vector-decomposition
> diagnostic (or, for a scalar output, presumably a similar unmasked-side-effect
> gap) not because vector decomposition itself is incomplete, but because the
> value it is being asked to decompose was never routed into the mechanism that
> already knows what to do with it. This is a *different* root cause from C3's
> own scope (which is genuinely closed -- see FeMeCPUDesign.md's deviation note)
> and from this row's existing matrix/aggregate-stage-IO entry (a
> `spirv`-\>`llvm` *conversion* gap, not a CPU-target *raising* gap); it is the
> largest single reason C3's own headline barely moved despite closing every
> producer/consumer shape "Vectors become components" describes
