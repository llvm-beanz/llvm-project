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

Can you work on L7s from the roadmap or other prerequisites blocking the
L-series milestones?

> **`dEQP-VK.subgroups.basic.compute.subgroupelect` fails runtime output
> verification ("0 / 7 values passed")**, split out of L7r's own closing
> session: L7r's own before/after regression check (comparing
> `subgroupelect`/`subgroupbarrier` with and without L7r's own `applyStageMasks`
> image-mask fix) confirmed this failure is pre-existing and entirely unaffected
> by that fix either way, but it is otherwise untracked anywhere in this
> document -- distinct from L7m's own tracked `subgroupbarrier`
> `DeleteDeadBlocks` assertion crash (a hard abort, not a runtime-value
> mismatch) despite both sharing the same `dEQP-VK.subgroups.basic.compute.*`
> group and both being newly reachable only once L7k's own array-deserialization
> fix let their shaders past deserialization for the first time. Needs its own
> real IR/output reduction (dumping the actual `tempResult`/`result[]` values
> this ICD's CPU runtime produces for `subgroupElect()`'s own masked-store path
> versus what the CTS verifier expects, mirroring the technique L7r's own
> session just used for the sibling `subgroupmemorybarrierimage` case) to
> isolate whether this is the same divergent-masking family of gap or something
> new
