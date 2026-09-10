---
model: claude-opus-5
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

Can you work on L89h from the roadmap or other prerequisites blocking the
L-series milestones?

> **`subgroupEqMask`/`GeMask`/`GtMask`/`LeMask`/`LtMask` builtins are
> unimplemented, failing all 10 `dEQP-VK.subgroups.builtin_mask_var.compute.*`
> cases**, found by L89f's own full-tree `dEQP-VK.subgroups.*` sweep -- the
> first such sweep this project has run, which is why these had not surfaced
> before (no previous session ran the `builtin_mask_var` group at all). All 10
> cases (5 mask builtins x plain/`_requiredsubgroupsize`) fail
> `vkCreateComputePipelines` with `VK_ERROR_INITIALIZATION_FAILED`. **Root cause
> confirmed by real IR reduction** (L89f's own closing session, after one wrong
> intermediate diagnosis was filed here and then caught -- see below): the five
> `SubgroupEqMask`/`GeMask`/`GtMask`/`LeMask`/`LtMask` SPIR-V builtin
> *variables* are simply absent from `BuiltInMappings[]` in
> `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp`, so a read of one
> falls through to the generic `Input`-storage-class-variable path and converts
> to a `feme.stage.input.load` at location 0, components 0-3 -- a *graphics*
> stage op that no compute-stage lowering handles, so the call survives all the
> way to codegen and the ORC JIT rejects the module with `JIT session error:
> Symbols not found: [ feme.stage.input.load.v4i32 ]`. They are gated by the
> `GroupNonUniformBallot` capability (which this ICD already advertises via
> `BALLOT_BIT`), and are builtin *variables* rather than `GroupNonUniform*`
> instructions, so they need builtin-variable decoding, not a conversion pattern
> for an op. Each is a straightforward function of the lane index against the
> ballot ABI's own 128-bit mask shape that `lowerBallot` in `WaveLowering.cpp`
> already produces (`EqMask` = `1 << lane`, `LtMask` = `(1 << lane) - 1`,
> `LeMask` = `LtMask | EqMask`, `GtMask` = `~LeMask`, `GeMask` = `~LtMask`, each
> truncated to the subgroup size). The one real design point is that
> `BuiltInMappings[]`'s own entry shape maps a builtin to exactly *one* LLVM
> intrinsic call, which cannot express any of these: each needs a small computed
> expansion producing a `vector<4xi32>` from
> `SubgroupLocalInvocationId`/`SubgroupSize`, so the fix needs its own
> conversion path alongside that table rather than a new row in it, plus unit
> coverage at the SPIR-V-to-LLVM conversion phase and a lit test. **A note on
> process**: this row's original text was correct, but L89f's own closing
> session briefly "corrected" it to blame `SIMDizePass` instead, on the strength
> of the *first* error a reduction surfaced. That error was real, but it was a
> second, independent gap layered in front of this one (fixed as L89h); only
> after fixing it did the actual missing-builtin failure become visible.
> Confirmed pre-existing and entirely unrelated to `SHUFFLE_BIT`: identical in
> both the before and after runs of L89f's own A/B sweep, and still 10/10
> failing after L89h
