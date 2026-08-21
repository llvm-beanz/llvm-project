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
after each change and update the VulkanCTSReport.md.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement milestone E25 in the roadmap document?

> **Broaden real per-format feature support**, the gap roadmap E24's own
> `deqp-vk` run surfaced rather than introduced:
> `dEQP-VK.api.info.format_properties.*`/`image_format_properties.*`/`unsupported_image_usage.*`
> now fail ~500 cases (up from `format_properties`'s own smaller pre-existing
> baseline) purely because `formatFeatureFlags` (Format.h) honestly reports most
> Vulkan-mandatory sampled/attachment formats as unsupported -- this ICD's CPU
> runtime only actually samples
> `R32G32B32A32_FLOAT`/`R8G8B8A8_UNORM`/`_UNORM_SRGB` (plus ASTC LDR, bridged)
> and its render-target pack/unpack table (`RenderPass.cpp`'s
> `isSupportedColorAttachmentFormat`) covers a narrower set than the Vulkan 1.0
> mandatory floor. Similarly, `dEQP-VK.*astc*`'s new 12,225 failures are mostly
> real texture-path gaps (e.g. `feme-cpu-simdize`'s divergent-vector limitation
> blocking `vktTextureTestUtil`'s fragment-shader cases), not query-accuracy
> ones. Closing this means extending the CPU runtime's typed sample table and
> `feme::graphics`'s pack/unpack table to the full mandatory format list -- a
> substantial, separate broadening of existing subsystems, not a single
> mechanical fix
