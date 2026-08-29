---
model: claude-sonnet-5
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

Can you complete H6m?

> **`vkQueueSubmit` fails a real
> `dEQP-VK.mesh_shader.ext.builtin.cull_primitives` case with `"stage element N
> has a 1-bit scalar; only 32-bit elements are implemented yet"`**
> (`StageStorage.cpp`'s own long-standing "32-bit scalars only" scope limit,
> `StageStorage.h`), newly reachable only because H6l's own fix lets this case
> clear `feme-graphics-validate-stage` for the first time. `gl_CullPrimitiveEXT`
> (`VK_EXT_mesh_shader`'s per-primitive cull builtin) is a SPIR-V/GLSL `bool`
> (`i1`), the element this case's own error names; nothing mesh-shading-specific
> about the gap itself -- any stage-IO element with a non-32-bit scalar type
> hits the same generic check (`StageStorage.cpp`, `SignatureElement::BitWidth
> != 32`) -- but this is the first real case in this project's own CTS coverage
> to reach it in the stage-IO path specifically (roadmap E29f already closed the
> related, but distinct, `Workgroup`-storage addressable-`i1` gap this generic
> limit does not cover). Needs a real IR reduction of this exact case (the same
> technique this whole H6g-b/H6j/H6k/H6l chain has used throughout) to scope a
> fix: whether `StageStorage`'s own per-element layout
> (`InvocationStride`/`ComponentStride`/`RowStride`, all hard-coded to a 4-byte
> scalar today) can widen to a 1-bit (or 1-byte, packed) scalar cleanly, or
> whether a `bool` stage-IO element should instead canonicalize to an ordinary
> 32-bit integer at the `CanonicalizeStage.cpp`/SPIR-V-to-LLVM boundary before
> ever reaching `StageStorage` at all (mirroring how a real GPU's own driver
> typically represents a shader-visible `bool` as a 32-bit value in memory)
