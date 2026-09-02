---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on H8l or other prerequisites blocking the H-series milestones?

> **BC7 compressed-format sampling** (split from H8k's own scoping pass; attempt
> before H8m/BC6H since it needs no half-float unquantization). Needs a new
> `BC7Decode.h`/`.cpp` (or an extension of `BCDecode.h`/`.cpp`, whichever proves
> cleaner once the per-mode bit-layout tables are drafted) implementing all 8
> BC7 modes per the Khronos `bptc.txt` specification: per-mode
> partition-selection bits (64 3-subset / 64 2-subset partition patterns, each a
> `4x4` texel-to-subset-index lookup table), rotation bits (modes 4/5 only,
> swaps a channel with alpha per-block), index-selection bit (modes 4/5 only,
> swaps which index field drives color vs. alpha), per-endpoint or shared P-bits
> (reconstructing the low bit of each endpoint channel before 8-bit
> replication-extension), and the anchor-index convention (the first index of
> any subset after subset 0 has one fewer bit, always implicitly 0, to break an
> encoding symmetry) -- mirroring how this project's own `ASTCDecode.cpp`
> already handles a comparable partition-table-driven decode. Standalone,
> directly-unit-tested, initially unwired, matching every decoder in this file
> family's own precedent
