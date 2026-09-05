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

Can you work on L44 or other prerequisites blocking the L-series milestones?

> **L43's own fix clears the `feme-cpu-simdize` divergent-branch diagnostic, but
> `dEQP-VK.mesh_shader.ext.query.no_queries.lines.no_reset.copy.no_wait.draw.32bit.no_availability.multiple_blocks.mesh_only.inside_rp.single_view.only_primary`
> still fails, now with a fatal `"LLVM ERROR: unsupported calling convention"`
> abort during JIT codegen (not a diagnosed `vkCreateGraphicsPipelines` failure
> -- the whole `deqp-vk` process aborts)**. Root-caused far enough to scope, not
> yet fully fixed: a real SPIR-V-imported mesh shader's own `OpControlBarrier`
> -- confirmed via the same captured pre-`LinearizePass` IR L43 used --
> compiles, via MLIR upstream's own default `ControlBarrierPattern`
> (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`, since
> `feme::spirv::populateSPIRVToLLVMTargetPatterns` installs no pattern of its
> own for `spirv::ControlBarrierOp`), to a call to a mangled external
> declaration, `declare spir_func void @_Z22__spirv_ControlBarrieriii(i32, i32,
> i32)` -- the exact same call form roadmap H4b's own `isSPIRVGroupSyncBarrier`
> (`feme/lib/Transforms/Graphics/CanonicalizeStage.cpp`) already recognizes by
> name for a *different* purpose (finding a tessellation-control entry's own
> patch-constant split point). `feme::cpu::matchBarrierCall`
> (`feme/lib/Transforms/CPU/BarrierCalls.cpp`) -- the function every
> stage-specific CPU wrapper pass (`EntryWrapper.cpp`, `GeometryWrapper.cpp`,
> `HullWrapper.cpp`, `DomainWrapper.cpp`, `PatchConstantWrapper.cpp`) actually
> calls to find and erase/fence-replace a real barrier before codegen -- only
> recognizes the DXIL/HLSL intrinsic forms
> (`llvm.dx.group_memory_barrier_with_group_sync` etc.), never this mangled call
> form, so for a mesh shader (unlike tessellation-control, which already has its
> own separate `isSPIRVGroupSyncBarrier` check, just for a different reason) the
> call survives completely unmodified -- still declared `spir_func`, with no
> real function body anywhere in the linked module -- all the way to X86 JIT
> codegen, which has no lowering at all for `CallingConv::SPIR_FUNC` and hits a
> `llvm_unreachable`-turned-fatal-error (`X86ISelLowering.cpp`'s
> trampoline-lowering `switch (CC)`, though the actual failing call site needs
> its own confirmation via a real backtrace, not yet captured). Needs: (1) a
> real `gdb`/backtrace confirmation of exactly which X86 lowering path aborts
> (the trampoline-`switch` `llvm_unreachable` found via a source grep is a
> strong candidate given the exact wording, but unconfirmed against a live
> stack), and (2) a fix most likely extending `feme::cpu::matchBarrierCall` (or
> a caller-side helper it can share with `isSPIRVGroupSyncBarrier`) to also
> recognize `_Z22__spirv_ControlBarrieriii` by name, the same way
> `isSPIRVGroupSyncBarrier` already does, so `EntryWrapper.cpp`'s existing
> barrier-erasure/fence-replacement logic actually reaches a mesh shader's own
> real barrier instead of leaving it as an unresolved external call
