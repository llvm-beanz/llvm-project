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

Can you work on L7m from the roadmap or other prerequisites blocking the
L-series milestones?

> **A real, pre-existing `llvm::DeleteDeadBlocks` assertion crash**
> (`llvm/lib/Transforms/Utils/BasicBlockUtils.cpp:159`: `Assertion
> `Dead.count(Pred) && "All predecessors must be dead!"' failed`), split out of
> L7k's own closing session: newly reached during
> `dEQP-VK.subgroups.basic.compute.subgroupbarrier` (and its
> `_requiredsubgroupsize` twin) now that L7k's own array-deserialization fix
> lets that shader's module past deserialization for the first time -- a crash
> in LLVM core code, not SPIR-V/`feme`-specific code, that aborts the entire
> `deqp-vk` process outright (losing all subsequent test results in that
> invocation unless the sweep is chunked per subgroup-category group to route
> around it, as this session's own re-verification had to do), consistent with
> this project's own documented precedent for this failure class (roadmap
> C2/H19p: "a crash silently truncates or corrupts a suite run"). Needs its own
> real IR reduction of the `subgroupbarrier` shader's own lowered LLVM IR
> (likely surfaced by whatever CFG-simplification pass this ICD's own lowering
> pipeline runs over a control-barrier-containing compute shader with
> divergent-looking control flow) to isolate the actual
> dead-block/predecessor-tracking bug, entirely independent of L7k's own
> array-deserialization scope
