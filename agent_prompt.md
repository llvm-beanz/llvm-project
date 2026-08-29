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
roadmap document to break down the remaining work for that milestone.

During the H6 milestone breakdowns things have gone a little crazy with nesting
letters in strange ways. Please avoid nesting milestones more than one lowercase
letter deep going forward.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you complete H6k?

> **A real `dEQP-VK.mesh_shader.ext.in_out.*` case now crashes with
> `SIGSEGV`/`SIGABRT` inside `feme::graphics::executeDraws` itself** (a
> heap-corruption `free()` of an `llvm::Expected<feme::graphics::StageStorage>`,
> per a real backtrace through `runPreparedDraw`/`runMeshDraw`/`vkQueueSubmit`),
> newly exposed by H6j's own interface-matching fix: once the 8-and-32 split of
> cases that fix unblocks reach real mesh-stage execution for the first time, 32
> of them crash the whole `deqp-vk` process rather than completing (cleanly or
> not) -- unlike H6c-a-a-iii's own previously-tracked arrayed-builtin-block
> crash (a clean, diagnosable assertion failure), this is silent heap corruption
> with no FeMe/MLIR diagnostic at all, only a bad `free()` several frames
> removed from wherever the actual overrun happened. Root cause not yet
> isolated: needs the same real-ICD-plus-`gdb`/reduced-IR technique this whole
> H6g-b/H6j chain has used throughout to find which `StageStorage`
> (`feme/lib/Graphics/StageStorage.{h,cpp}`) allocation/`writeRaw`/`readRaw`
> call in the mesh-to-fragment `copyLinkedElements`/varying path over- or
> under-sizes a buffer for a mesh entry's own per-vertex output count, and
> whether the fix belongs in `StageStorage` itself, `Executor.cpp`'s own
> mesh-specific `RasterSig`/varying-linking setup, or `MeshOutputWrapperPass`'s
> own per-vertex output layout
