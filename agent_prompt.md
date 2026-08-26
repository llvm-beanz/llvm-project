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

Can you work on milestone C8?

> **Shader long tail.** Descriptor arrays of combined image samplers (816),
> matrix/aggregate stage IO (309), the SPIR-V importer's `unhandled opcode` set
> (171), `Workgroup` arrays-of-arrays (151), the 277 individually-unlegalized
> ops (`spirv.SpecConstant`, `spirv.VectorExtractDynamic`,
> `spirv.CompositeConstruct`, the `spirv.Atomic*` family, ...), and the 242-case
> diagnostic tail. Best attacked *after* C2/C3, since the true size of this
> bucket is unknown until the stacked blockers ahead of it are gone. ~~**C3's
> own measurement found a new member of this bucket, larger than any row already
> in it**: a plain, non-atomic `load`/`store` on a raw SPIR-V
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
> producer/consumer shape "Vectors become components" describes~~ (done: the
> actual root cause was narrower, and more general, than this finding's own
> framing -- it was never specific to SPIR-V.
> `feme::graphics::CanonicalizeStagePass` (the pass that rewrites both a DXIL
> `loadInput`/`storeOutput` call *and* a raw SPIR-V
> `Input`/`Output`-storage-class global load/store into `feme.stage.*`) was
> never run by `feme::cpu::runPipeline` *at all* -- only by the separate Vulkan
> graphics pipeline (`GraphicsPipeline.cpp`), which every real CTS draw call
> goes through, but which the CPU-target pipeline `feme-run`/`feme-cpu-simdize`
> measure against does not. A DXIL-imported fragment/vertex shader reaching this
> pipeline directly hit the exact same gap SPIR-V's did
> (`checkSupportedRaisedOps` diagnosing the still-raw
> `dx.op.loadInput`/`storeOutput` call outright, rather than
> `feme-cpu-simdize`'s vector-decomposition diagnostic reaching a raw store the
> way SPIR-V's did) -- the roadmap text's framing of DXIL's stage IO as "always"
> canonicalized this way did not hold once traced through this pipeline
> specifically, only through the Vulkan one. `runPipeline` now runs
> `CanonicalizeStagePass` immediately before `ValidateStagePass`, matching the
> ordering "CPU Lowering Pipeline"'s own diagram in FeMeGraphicsDesign.md
> already drew, closing this gap for both import paths with one change
> (`feme/lib/Target/CPU/Pipeline.cpp`); see that document's updated status note
> for the full deviation record and `unittests/Target/CPU/PipelineTest.cpp`'s
> `CanonicalizesRaw{SPIRV,DXIL}StageIOBeforeWidening` for the two regression
> tests, each first confirmed failing (in its own distinct way) against the
> pre-fix pipeline. Measured against a real `deqp-vk` run, this fix moves
> **nothing**: `feme::vulkan::compileGraphicsStage` (`GraphicsPipeline.cpp`)
> already calls `CanonicalizeStagePass` directly, since roadmap V6, before every
> real `vkCreateGraphicsPipelines` call reaches `runPipeline` at all, so no
> `dEQP-VK` case was ever routed through the gap this fix closes -- it only
> mattered for `feme::cpu::JITEngine`/`feme-run`'s direct entry points. The C3
> section's own attribution of its stalled headline to this finding does not
> hold once measured, and the rest of C8's bucket -- descriptor arrays of
> combined image samplers, matrix/aggregate stage IO, the remaining
> `spirv`-\>`llvm` conversion gaps, and the broader unlegalized-op/diagnostic
> tail -- remains open, exactly as large as before; see VulkanCTSReport.md's
> "Roadmap C8: measured impact" for the full before/after comparison)
