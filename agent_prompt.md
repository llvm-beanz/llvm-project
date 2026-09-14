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


Can you work on the H-series milestones?

The previous session suggested the next steps:

1. **H111(b)** (~30-45 min for a first diagnostic): the newly-exposed all-black
   `fullscreen_gradient` render. Since the SPIR-V-to-LLVM-dialect conversion
   output looks correct by inspection, use `feme-run` (the CPU JIT/dispatch
   runner) or a channel-level pixel/IR reduction (mirroring H88's own technique)
   further downstream through feme's own CPU lowering passes and execution to
   find exactly where `positions[vertex]`/`colors[vertex]` -- read from a local
   `alloca` of an array-of-vectors via a dynamic GEP index -- stops carrying the
   right value. This is a shape (function-local, not stage-IO-global, array
   addressed dynamically) that no prior CTS case ever reached, so it may be a
   real gap in CPU codegen rather than a one-line fix.
2. Once H111(b) lands, re-run the 3 `fullscreen_gradient` cases plus the broader
   `mesh_shader.ext.*` sweep (26,921 cases) to confirm 439/439 of the
   currently-`Supported` cases are green -- this would fully close out H70's
   whole lineage (H93 -> H108 -> H109 -> H110 -> H111).
3. No other blocking work was found this session -- H110 and H111(a) were the
   only two items left on the prior session's list, and both are now closed.
