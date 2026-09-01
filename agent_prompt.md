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

Please investigate and fix the issues tracked by milestone L17:

> **A fixed-size array of a *scalar* type inside a struct/cbuffer, immediately
> followed by another sibling member, is not handled by L13a's own
> `convertArrayTypeIgnoringDecorations`** -- deliberately left out of that row's
> own scope (see its doc comment). The real shape
> (`Feature/CBuffer/array-of-structs.test`/`dynamic-struct.test`'s own `uint
> x[2]; uint q;`) is `struct<S, (array<2 x i32, stride=16> [0], i32 [20])>`:
> real HLSL/Vulkan layout semantics require every array element *except the
> last* to occupy the full declared stride (so a dynamic index into `x[1]` still
> addresses correctly), but the array's own occupied footprint for a
> *subsequent* sibling member's own placement is only `(N-1)*Stride +
> NaturalSize(last element)` -- i.e. `q` is allowed to pack into the
> otherwise-unused trailing padding of `x`'s own last element. A single,
> homogeneous `LLVM::LLVMArrayType` cannot represent "N-1 stride-wide elements
> plus one natural-size element" at all (every element, including the last, must
> be the same width); representing it instead as a heterogeneous struct
> (`struct<(array<(N-1) x Padded>, Unpadded)>`) cannot support a *dynamic*
> (runtime-value) index selecting between the array portion and the
> final-element portion, since LLVM `getelementptr` struct-member selection
> requires a compile-time-constant index -- a materially harder problem than
> L13a's own append-only padding. Needs its own scoping pass to determine
> whether a dynamic index into such an array can be legalized at all without
> either (a) always widening the *last* element to the full stride too
> (over-allocating by `Stride - NaturalSize(last)` bytes, wasting space but
> preserving a uniform array and requiring no representation change), or (b) a
> real heterogeneous-with-dynamic-index scheme this codebase does not have a
> precedent for anywhere yet
