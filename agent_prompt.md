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

Plese work on roadmap step E29:

> **The same full run (E27/E28) found six more distinct, reproducible crashes**
> (`SIGSEGV` in `api.granularity.*`,
> `glsl.texture_functions.query.texturesamples.*`, `image.subresource_layout.*`,
> and `synchronization.timeline_semaphore.*`; an `llvm_unreachable` in
> `ResourceCalls.cpp` for an unsupported `feme.cpu.resource.*` element type
> reached from `spirv_assembly.instruction.spirv1p4.opselect.array_select`; an
> `llvm::Value::setNameImpl` assertion from `renderpasses.dynamic_rendering.*`;
> and a `GetElementPtrTypeIterator.h` assertion from
> `compute.pipeline.zero_initialize_workgroup_memory.*`), each aborting its own
> top-level `dEQP-VK` group partway through the same way the long-documented
> `api` crash already did. None investigated past its own crash-site log line
> (see VulkanCTSReport.md's "Full run, roadmap E27/E28" section for what is
> known about each) -- unlike E27/E28, no root cause is confirmed yet for any of
> these six, so none is claimed fixed here. Six independently assignable rows
> once triaged, the same granularity as every other row in this section | none
> (six independent crashes; split into per-crash rows once triaged)
