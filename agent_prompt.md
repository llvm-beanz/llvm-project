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

Can you work the H-series milestones?

The last session suggested the next steps:

1. **H116** (~45-60 min, still untriaged across ~5 sessions now):
   `per_patch_array.*` (9 cases), "Invalid input value in tessellation
   evaluation shader" -- a different error class from H117/H118, never
   looked at in isolation. Only 9 cases and the last open row in the
   `user_defined_io` matrix, likely the fastest remaining close.
2. **`offload-test-suite`'s `check-hlsl-feme-vk` target** (well over a
   dozen sessions deferring this now): still never built/run in any
   session on record. Give it a dedicated session with no competing
   priority.
3. **The `transform_feedback.fuzz.random_geometry.all_instance_array.12`
   pre-existing heap corruption** (~1-2 hours, real bug, now clearly
   isolated): confirmed pre-existing and unrelated to H117/H118, not
   fixed this session (out of scope for the H117/H118 task). The
   `valgrind` trace already points at `buildStageStorage`/`executeDraws`
   allocating a too-small buffer for this fuzzed multi-member XFB
   block-array shape -- worth its own roadmap row and a dedicated
   session, since `valgrind`'s own stack trace is a strong head start.
4. Low priority: no scratch files left to clean up (this session's own
   were removed, including a stray `tese.spv`).
