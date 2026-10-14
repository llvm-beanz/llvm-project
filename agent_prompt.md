---
model: claude-sonnet-5
resume: b8ce7a07-6a6f-4205-864b-b09621346aaf
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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~1-2 days)** `variablePointers` is `K6a`'s only remaining bit. Start from
   the prior investigation's exact failure list (`PtrAccessChain` "explicitly
   marked illegal", unhandled `OpPtrEqual`/`OpPtrNotEqual`, `OpConstantNull` on
   pointer types) rather than re-deriving it -- two sessions already confirmed
   empirically it's a real lowering gap, not a quick flip.
2. **(~1 day)** New `K6j`: the residual
   `single_nested_struct`/`random.scalar.83` nested-struct scalar-layout gap
   this session found (`VK_ERROR_INITIALIZATION_FAILED` on a struct containing a
   nested-struct member at a scalar-tight offset). Not yet grep-traced -- likely
   needs the same kind of fix as this session's bugs #1/#2 but for
   `convertOffsetStructTypeIgnoringDecorations`'s nested-struct-member handling
   specifically, not its vector/matrix-member handling (which this session
   fixed).
3. **(standing, unresolved many sessions)** `backup-before-rebase-20261014` tag
   decision, `CMAKE_BUILD_TYPE=RelWithDebInfo` vs `Release` decision -- neither
   revisited this session either.
4. **(standing)** `L419(j)` remaining untriaged items, `L418(a)` sanity
   re-check, deferred full `Vulkan14FeatureInventory.md`/extension-manifest
   regeneration
   (`AdvertisedExtensions.txt`/`CorePromotedNotAdvertisedExtensions.txt` input
   files still stale from `K6b`-`K6h`'s own prior sessions).
5. **(standing)** offload-test-suite `VkBool32`/`WaveActiveMax` pattern check --
   not exercised this session either, no `check-hlsl-feme-vk` work done (session
   stayed scoped to the single `K6i` compiler-fix task).

**Immediate next action for next session:** re-read `Roadmap.md`'s now-1-bit
`K6a` row, pick `variablePointers`, and start from the exact failure list two
prior sessions already logged (`PtrAccessChain`/`OpPtrEqual` family) -- or pivot
to the new `K6j` nested-struct gap if a break from `K6a` is wanted.
