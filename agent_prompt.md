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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L7k from the roadmap or other prerequisites blocking the
L-series milestones?

> **A pre-existing upstream MLIR SPIR-V deserializer limitation rejects an
> `OpTypeArray` whose length operand is a specialization constant**
> (`mlir/lib/Target/SPIRV/Deserialization/Deserializer.cpp`'s
> `processArrayType`: "OpTypeArray count <id> ... can only come from normal
> constant right now", a TODO already marked verbatim in that function,
> confirmed via a direct `deqp-vk` re-run reduction), split out of L7j's own
> closing session this session: several
> `dEQP-VK.subgroups.basic.compute.*`/`arithmetic.*`/`clustered.*`/`shuffle.*`
> shaders declare a workgroup-size-derived shared-memory array (e.g. sized by
> `gl_WorkGroupSize.x` or a subgroup-count spec constant) as an `OpTypeArray`
> whose length operand is itself an `OpSpecConstant`, not an ordinary
> `OpConstant`, well upstream of this project's own SPIR-V-to-LLVM conversion
> passes (the deserializer rejects the module before any `feme`-specific pass
> ever runs). Needs: (1) a real IR reduction of one confirmed-failing case (e.g.
> `dEQP-VK.subgroups.basic.compute.subgroupelect`) to confirm the exact
> array/spec-constant shape, (2) a scoping decision on whether to patch upstream
> MLIR's own deserializer directly (recording the spec-constant `<id>` as the
> array's dynamic length, resolved via this project's own new
> `feme::spirv::SpecConstantValueMap`/`prepareSpecConstants` from L7j once the
> module is fully deserialized, or via some other MLIR-upstream-appropriate
> mechanism) versus a `feme`-side workaround, and (3) a real `deqp-vk` re-run of
> `dEQP-VK.subgroups.*.compute.*` once fixed to measure the real pass-rate
> improvement, plus a fresh look at whether the remaining
> runtime-value-verification gap L7j's own sweep also surfaced (e.g.
> `dEQP-VK.subgroups.builtin_var.compute.subgroupsize_compute`, "2 / 7 values
> passed") is related or entirely separate
