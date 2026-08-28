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

Can you complete and close out milestone H6c-a-a-iii?

> **Fix `CanonicalizeStage.cpp`'s `resolveOffsetWithinElement`, which asserts
> (`cast<StructType>`) rather than gracefully rejecting an unmodeled shape when
> a multi-`ElementID` builtin interface block's own value type is not a plain
> (non-arrayed) `StructType`** -- reachable for the first time by H6c-a-a-i's
> own closing re-run, since a mesh entry's `PerPrimitiveEXT`/other arrayed
> builtin interface blocks (e.g. an array-of-struct per-primitive output block)
> take exactly this shape, and previously never got this far because an
> unconverted `SetMeshOutputsEXT` always failed earlier, at SPIR-V-to-LLVM
> conversion. 28 of `dEQP-VK.mesh_shader.*`'s own cases now abort the whole
> `deqp-vk` process with this assertion instead of failing cleanly with
> `VK_ERROR_INITIALIZATION_FAILED` the way they did before H6c-a-a-i landed
> (same failing-case *set*, just a worse failure *mode* -- 0 `Pass`/`Fail`
> regressions, but a real robustness regression a fuzzer or a CTS run without
> this report's own resume-loop workaround would trip over)
