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

Can you work the the H-series milestones?

The last session suggested the next steps:

1. **File milestone rows for the two bugs above** (~15 min each just to
   write the row; real fix time unknown, no diagnostic done yet on
   either). The `spirv_var_N` one especially -- it's been rediscovered
   and deferred across at least 5 sessions in a row without ever
   getting a row of its own.
2. **`offload-test-suite`'s `check-hlsl-feme-vk` target**: still never
   built or run in any session on record. This is a standing gap, not a
   quick add-on -- give it a dedicated session.
3. **Re-triage the roadmap from scratch** for the next H-series pick.
   With H32/H53/H7w/H7x all closed, the "shaderClipDistance/
   shaderCullDistance" thread that dominated the last several sessions
   is fully wound down. Check `Roadmap.md` for the next open P1/P2 row
   (last known: H62 broken into H63-H68 sub-buckets, ~14-48 cases
   apiece, each independently assignable).
4. Clean up `/tmp/h7x_*` scratch files (low priority, not part of the
   repo).
