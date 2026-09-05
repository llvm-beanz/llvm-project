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
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

The last session got stuck.

Can you work on L45 or other prerequisites blocking the L-series milestones?

> **L44's own fix clears the fatal `"LLVM ERROR: unsupported calling
> convention"` abort, but
> `dEQP-VK.mesh_shader.ext.query.no_queries.*.mesh_only.*`/`.task_mesh.*` (all 4
> real, feature-supported cases in the sweep L44 ran) now fail cleanly at
> `vkCreateGraphicsPipelines` with a distinct, later, diagnosed error**:
> `"feme-cpu-wrap-entry: function 'main' has a barrier inside non-linear control
> flow (a surviving branch not part of a supported loop); region splitting only
> supports a straight-line wave body or a single uniform loop (roadmap milestone
> 9 deviation)"`. Root-caused via a real captured pre-`EntryWrapperPass` IR dump
> of the exact CTS case (same env-gated `FEME_DEBUG_DUMP_PIPELINE_STAGE_IR`
> technique, temporary, reverted before committing):
> `feme::cpu::EntryWrapperPass::splitAtGroupSyncBarriers` first tries
> `matchLoopShape` (the "barrier inside a uniform loop" shape), which correctly
> declines without diagnosing (this function has no loop at all), then falls
> back to `isLinearChain`, which requires the *entire* function to be one
> branch-free, loop-free straight chain from entry to `ret` -- and rejects this
> real shader outright, since its own single barrier sits in the entry block,
> safely *before* an entirely unrelated, ordinary uniform diamond further down
> (`%push_const.inbounds = icmp ule i32 4, %root_constant_size` deciding whether
> to load a real root-constant value or default to 0 -- itself containing no
> further barrier, and never spanning the barrier boundary in either arm).
> `isLinearChain`'s all-or-nothing check cannot distinguish this safe case (a
> uniform branch entirely contained within one barrier-delimited region) from a
> genuinely unsafe one (a branch whose two arms would land in *different*
> barrier regions, which `outlineChain`'s current flat-block-list design has no
> way to represent as two still-connected region functions). Needs: (1)
> confirmation (via further real CTS cases) of whether every occurrence of this
> diagnostic is this same "uniform diamond fully outside every barrier region"
> shape, or whether a genuinely barrier-spanning branch also occurs somewhere in
> the wild, and (2) a real design decision on the fix's shape -- most likely
> teaching `isLinearChain`/`splitAtGroupSyncBarriers` to first identify each
> barrier's own region boundary and permit an arbitrary (uniform-only,
> side-effect-free-enough) sub-CFG *within* a single region, only rejecting a
> branch that actually straddles one, which would require generalizing
> `outlineChain` (currently a flat `SmallVector<BasicBlock *>` chain, assuming
> an unconditional-branch-only interior) to outline a real multi-block sub-CFG
> per region instead -- a materially bigger change than L43/L44's own scope,
> likely its own multi-part milestone rather than a single small fix
