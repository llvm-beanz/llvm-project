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

Can you work on H94a or other blocking work to make progress on the H-series
milestones?

> **Teach `matchExitCheckWithRelay`/`DivergentCandidates` to collapse a chain of
> `StructurizeCFG`-built boolean-relay `CondBr` blocks (`Flow`,
> `loop.exit.guard`, and similar) back down to the single real, data-dependent
> divergent check they all just re-encode**, rather than counting each relay hop
> as its own independent divergent candidate -- see H94's own triage write-up
> for the exact shape (a real check whose exit decision reaches the loop's exit
> block only through one or more further `CondBr` relay blocks, each provably a
> pure boolean function -- identity or negation, established via which of the
> real check's two successors, or a straight chain from one of them, feeds each
> relay's own incoming edges -- of that same one real decision). Needs: (1) a
> dominance-respecting rewrite step (extending `peelConstantFlowPredecessors`'s
> existing "peel a literal-constant incoming edge" idea, roadmap L40) that
> replaces a relay block's own boolean-relay `phi` with a directly-inserted
> `xor`/identity use of the real check's condition, legal exactly when that
> condition's defining block dominates the relay's own block (true whenever, as
> in this case, the real check dominates every block on both of its own
> successor's paths back to the relay); (2)
> `DivergentCandidates`/`matchExitCheckWithRelay` recognizing, after that
> rewrite, that multiple `OtherCondBrBlocks` entries reducing to the *same*
> underlying condition (up to negation) are one check, not several, and picking
> whichever of them (not necessarily the original real check's own block)
> actually reaches the loop's exit/backedge in a
> `matchExitCheckWithRelay`-recognizable way; (3) verifying `linearizeCycle`'s
> later mask-application logic
> (`applyStageMasks`/`collectUniformPassThroughRegion`) correctly threads masks
> through *every* relay hop in the chain, not just a single one, since this
> shape can have more than one (this case's own `Flow` then `loop.exit.guard`);
> (4) regression coverage at both the `Linearize.cpp` unit-test level (a
> hand-built two-relay-hop `.ll` case, mirroring L40/L42/H89a/H89b's own
> precedent) and a real CTS re-run of all 4 of H94's own cases confirming they
> compile and pass. Not yet started -- this is real, nontrivial `LoopLinearizer`
> capability work (touches classification, a new dominance-respecting IR
> rewrite, and the mask-threading walk all at once), not a quick follow-on patch

The previous run suggested next steps:

## Suggested next steps (in order, if resuming this work)

> 1. Check whether `/tmp/h94dump/module0-prepared.ll` still exists; if not, redo the capture
>    (steps 2-3 above) -- budget about half a day for the reduction alone if starting cold.
> 2. Check the reduced `.ll` into the repo (e.g. `feme/test/Transforms/CPU/loop-relay-chain.ll`,
>    marked `XFAIL` or similar for now) so the reduction itself is never lost or needs redoing.
> 3. Implement the dominance-respecting relay-collapse rewrite described in H94a (a new small
>    step, analogous to the existing `peelConstantFlowPredecessors`, that rewrites a relay
>    block's boolean phi into a direct `xor`/identity use of the real check's condition).
> 4. Re-run `DivergentCandidates` classification against the now-collapsed IR; confirm it drops
>    to exactly one real candidate for this shape.
> 5. Check `matchExitCheckWithRelay`/`linearizeCycle`'s mask-threading logic against the
>    now-single-candidate IR -- it may already "just work" once collapsed, or may need its own
>    change to handle the (still-present, just now-irrelevant-to-classification) extra hops.
> 6. Add a `Linearize.cpp` unit test with a hand-built two-relay-hop `.ll` case before touching
>    the real CTS cases, to get fast iteration without the full CTS build/run loop.
> 7. Re-run all 4 of H94's own CTS cases for real pass/fail, not just "no crash."
