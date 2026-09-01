---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Please investigate and fix the issues tracked by milestone L11:

> **`SIMDize.cpp`'s `widenGroupSharedLoad` does not support a vector-typed
> result for a divergent groupshared address** -- found as an L10
> milestone-description correction:
> `WaveOps/GroupSharedMatrixRowComponentDataRace.test` was grouped under L10's
> own "unrecognized broadcast"/"nested getelementptr" family, but its real
> failure is a distinct, already-documented (in a `SIMDize.cpp` comment, ~line
> 745: "A groupshared address keeps its own dedicated gather-based widening
> (`widenGroupSharedLoad`), which does not (yet) support a vector-typed result")
> scope gap: a divergent groupshared address whose load result is a full row
> (e.g. `float4`, not a scalar) has nowhere to go once `SIMDize.cpp`'s own
> general divergent-value handling reports "function 'main' has a divergent
> value '' of vector type" instead. Needs its own scoping pass to determine
> whether `widenGroupSharedLoad`'s existing scalar gather-based approach extends
> cleanly to a vector element type (likely a per-lane gather of the whole row,
> then a `shufflevector`/`insertelement` assembly, mirroring how the scalar case
> already gathers one lane at a time) or needs a different strategy entirely
