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

Can you work on L7o from the roadmap or other prerequisites blocking the
L-series milestones?

> **A `feme-cpu-simdize` "groupshared global ... feeds a nested getelementptr or
> another unsupported user" diagnostic newly reached by
> `dEQP-VK.subgroups.basic.compute.subgroupelect`/`_requiredsubgroupsize`**,
> split out of L7l's own closing session: now that L7l's own
> `spirv.MemoryBarrier` legalization fix lets this shader's module past
> pipeline-creation legalization for the first time, it reaches
> `GroupShared.cpp`'s `rewriteGroupSharedGlobals` validation for the first time
> too, and is declined by the same "only a first-level getelementptr feeding a
> direct load/store/atomicrmw/masked gather-scatter, or a vector-row-load's
> second-level per-component getelementptr, is supported" check roadmap L10/L11
> already narrowed the scope of (`feme/docs/Roadmap.md`'s own L10/L11 rows) --
> but this is a new, real, previously-unseen occurrence of that same diagnostic
> family, not a regression in L7l's own fix (`subgroupelect`'s shader accesses
> its own `tempShared`/`tempBuffer` groupshared array via
> `subgroupElect()`-gated indexing, a shape distinct from either of L10/L11's
> own already-fixed offload-test-suite repros). Needs: (1) a real IR reduction
> of `subgroupelect`'s own lowered LLVM IR (the same
> `FEME_DEBUG_DUMP_PIPELINE_STAGE_IR`-gated technique L45's own closing session
> used) to see the exact GEP-nesting shape this shader's indexing produces, and
> (2) a scoping decision on whether it's a third distinct shape needing its own
> `rewriteGroupSharedGlobals`/`widenGroupSharedLoad` extension (mirroring L11's
> own vector-row-load precedent), or reachable via some non-`GroupShared.cpp`
> restructuring upstream of it instead
