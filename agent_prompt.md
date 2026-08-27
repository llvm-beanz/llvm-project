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

Can you work on milestone H4e?

> **`MaskIntrinsics.cpp`'s `appendScalarMangling` calls
> `llvm_unreachable("unsupported feme.cpu.masked.* element type")` for any
> masked-load/store element type it does not recognize (matrix/aggregate shapes
> among them), aborting the whole process instead of diagnosing gracefully** --
> newly reached (not introduced) by H4c's own fix: all 24 of H4c's named cases
> now split successfully and reach `feme::cpu::SIMDizePass`'s masked-memory-op
> path, which is very likely the same C8-bucket matrix/aggregate-legalization
> gap H4b's own triage table already names for the 88 `feme-cpu-simdize: ...
> divergent value ... of vector type` and 74 `llvm.getelementptr` cases (not yet
> independently confirmed), just reached through a third path. Needs both the
> root legalization fix (C8's own scope) and, independently and more urgently,
> hardening this specific function to `emitError`+return a null/sentinel rather
> than `llvm_unreachable`, so an unsupported shape becomes a graceful per-case
> `Fail` the way every other gap in this codebase already does, rather than a
> `SIGABRT` that kills an entire CTS run's remaining cases
