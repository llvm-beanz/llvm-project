---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

# Request

Can you work on H101k or other blocking work to make progress on the H-series
milestones?

> **`transform_feedback.fuzz.*instance_array*`'s remaining 68
> `VK_ERROR_INITIALIZATION_FAILED` pipeline-creation failures** (confirmed
> reproducible this session, during H101j's own closing sweep -- unchanged in
> count and symptom from H101i's own closing note, not caused by or affected by
> H101j's marker-struct fix): e.g. `random_geometry.all_instance_array.11` fails
> pipeline creation with `JIT session error: Symbols not found: [ spirv_var_46
> ]` printed before the `VK_ERROR_INITIALIZATION_FAILED`, suggesting an
> unresolved stage-IO global-variable reference somewhere in the JIT-compiled
> shader for this specific multi-member-block-with-nested-array-member shape
> family -- likely the same nested-array-member SPIR-V-to-LLVM
> legalization/lowering gap H101i's own closing note flagged as still open (a
> case that legalizes but produces an unresolved symbol reference downstream),
> rather than a new, distinct bug. Not yet triaged -- needs a standalone
> `feme-translate` repro of one such shader's LLVM IR (mirroring H101a's own
> JIT-bypass technique) to identify what `spirv_var_46`-shaped global reference
> is left unresolved and trace it back to whichever conversion/canonicalization
> pass fails to materialize or wire it up
