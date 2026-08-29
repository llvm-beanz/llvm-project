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

Can you complete H6j?

>**A real `dEQP-VK.mesh_shader.ext.in_out.*` case now fails at `vkQueueSubmit`
>with "vertex output and fragment input at location 0 disagree on component/row
>count or type"**, newly exposed by H6g-b-d's own `MeshOutputWrapperPass`
>catch-all fix: once the 40 cases that fix unblocks progress past compilation,
>they reach submission-time interface validation between the mesh entry's own
>per-vertex output and the fragment stage's input, and disagree despite both
>sides being generated from matching `layout(location=...)` declarations in
>`vktMeshShaderInOutTestsEXT.cpp`'s own generated GLSL. Root cause not yet
>isolated: unclear whether the mismatch is a genuine signature-matching bug
>specific to a mesh-to-fragment interface (as opposed to the already-working
>vertex-to-fragment and geometry-to-fragment cases), a
>`MeshOutputWrapperPass`/`EntryWrapperPass` byproduct that changes the reflected
>output signature's own component/row count from what the fragment stage's input
>signature expects, or a pre-existing interface-matching gap this is simply the
>first mesh-stage case to exercise at all
