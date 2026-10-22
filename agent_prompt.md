---
model: claude-sonnet-5
resume: 1b814ab3-f423-4724-ae98-f676112844de
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file, and the environment-wide
agent skills at /home/dev/.agents/skills.

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

**Before doing anything else**: `vulkaninfo --summary | grep deviceName` and
confirm `FeMe CPU Vulkan Device`. Every session from now on, every time, not
just once at the start.

**When you find an issue outside FeMe**: Create an isolated reproducer, fix it,
and apply the fix in a commit that only touches files from outside the FeMe
subdirectory. Ensure that fixes to other LLVM sub-projects are self-contained
and tested.

**When you find an issue with environment or process**: Codify it in Feme's
.instructions.md file so future sessions don't lose it.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

**Always**: End each session with a commit of the agent thoughts even if
mid-investigation to avoid losing documentation.

# Request

Please continue working on the FeMe Vulkan ICD. The previous session suggested
the next steps:

## Two candidate fix directions for next session (pick one, prototype against BOTH reproducers)

1. **(a) Extend the existing peel guard.** `peelConstantFlowPredecessors` already has `peelWouldDestroyAncestorReconvergencePoint`, which looks *backward* from the sole surviving predecessor for an ancestor `CondBr` whose `validate` could break. Add a *forward*-looking variant: before peeling, check whether the peel's `Target` would become newly shared between the peeled chain and some other still-unflattened nested `CondBr`'s sibling arm; if so, bail (skip just that one peel, leave the harmless-redundant IR in place for `H165` to flatten normally).
2. **(b) Local un-peel-and-retry in `H165`.** When `validate` rejects a pass-through block with >1 predecessor, check whether undoing *just that one* peeled edge (not all peeling cycle-wide) would let `flattenLoopBodyDiamond` succeed; if so, locally reverse it and retry.

Validate against **both** `/tmp/relay_frozen2/repro.ll` (`(g)`, must stay Pass) and `/tmp/mergesort_discard_repro/repro.ll` (`(i)`, must flip to Pass), then the full 106-case graphicsfuzz corpus, then `check-feme`, before considering either landed.

## Carried-forward backlog (unchanged, not investigated this session)

- `ENV6(c)(2)(f)` (1 case, unexplained non-dominance `Fail`, not started)
- `ENV6(c)(2)(a)` (7-case all-black-render bucket, investigate fresh)
- `ENV6(c)(2)(c)`/`(d)` (two isolated 1-case issues, not started)
- `ENV6(c)(3)` (83 cases), `ENV6(d)` (132 cases) -- own multi-session efforts
- `ENV5(c)(7)(b)` (hull lockstep barrier, 3 cases) -- lowest priority
- offload-test-suite upstreaming (3 patches, blocked on push access)
