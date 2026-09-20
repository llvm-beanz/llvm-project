---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue the work on feme? The last agent's suggested next steps are:

## Next steps

1. **(~2 min)** Nothing to clean up -- this session's own scratch CTS
   logs (`/tmp/ctsrun/l125h_*`) are already deleted.
2. **L125(g)** is the last item in this sub-tree: `SampleCmp*`/
   `Gather*`/`GatherCmp*` swizzle semantics. Needs spec research
   *before* any code change -- specifically: (a) does a depth-compare's
   single-channel dref read honor a non-identity `VkComponentMapping`
   at all, or is depth-compare exempt since it reads a specific
   depth-format channel rather than an RGBA color? (b) does
   `OpImageGather`'s `Component` (0-3) selector operate on the
   pre-swizzle or post-swizzle channel layout? Check the Vulkan spec's
   own "Image Operations" chapter and `vktPipelineImageViewTests.cpp`/
   `vktImageGatherTests.cpp` for any existing coverage before assuming
   either answer.
3. Consider the new `VK_ERROR_INITIALIZATION_FAILED`-on-plain-`_sint`-
   sampling data point discovered this session as a fresh lead for
   L125(c)'s own untriaged buckets -- it may share a root cause with
   the already-known `sampler.border_swizzle.*`-heavy
   `VK_ERROR_INITIALIZATION_FAILED` bucket from that row's original
   triage, worth checking before assuming they're the same or
   different bugs.
4. `L125(c)`'s own remaining buckets (ASTC/EAC/ETC2/BC image
   mismatches, the two distinct `VK_ERROR_INITIALIZATION_FAILED`
   sites, `vktPipelineBindPointTests.cpp`) remain untouched and
   untriaged -- a good alternative pick if L125(g)'s spec research
   doesn't pan out quickly.
5. `ninja check-feme` and both CTS build directories are incremental
   from here -- no reconfigure needed.
