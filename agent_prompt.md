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
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on milestone H3a?

> **`gl_ViewportIndex` read back as a fragment-shader input fails pipeline
> creation** (roadmap H3's own Deviation,
> `dEQP-VK.draw.*.shader_viewport_index.fragment_shader_*`, 68 of H3's own
> measured 196 cases, `GL_ARB_shader_viewport_layer_array`'s other half:
> `out_color = color[gl_ViewportIndex]`): `vkCreateGraphicsPipelines` fails with
> `VK_ERROR_INITIALIZATION_FAILED`, `FragmentWrapper.cpp`'s
> `lowerFragmentStageOps` reporting "fragment stage wrapper requires attached
> feme.signature metadata" -- i.e. the fragment entry function reaches code
> generation with no `feme.signature` metadata attached at all, not merely a
> mismatched one. `CanonicalizeStage.cpp`'s builtin-to-`SignatureSystemValue`
> mapping (`getSystemValueForBuiltIn`) already maps SPIR-V `BuiltIn` 10
> (`ViewportIndex`) to `SignatureSystemValue::ViewportArrayIndex` regardless of
> storage class, so the reflection-side mapping itself looks direction-agnostic;
> root cause not yet isolated to a single line, needs its own investigation into
> why the fragment stage's globals loop (`CanonicalizeStage.cpp`'s
> `InputGlobals`/`OutputGlobals` collection) does not see this variable at all
> for a fragment-shader `Input`-storage-class `ViewportIndex` builtin, unlike
> the small set of fragment-input builtins already wired up before this
> milestone (`FragCoord`/`Position`, `FrontFacing`, `SampleId`, `SampleMask`)
