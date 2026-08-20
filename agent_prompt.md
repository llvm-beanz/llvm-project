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

Can you complete milestone D2 on the roadmap?

> **The system Vulkan loader crash D0's second CTS pass found.**
> `dEQP-VK.api.object_management.multithreaded_per_thread_resources.*`, run as
> one sequence, segfaults inside Ubuntu's `libvulkan1` (`vkGetDeviceProcAddr`,
> called from concurrent `vkCreateDevice`s) -- not inside any FeMe code, and
> does not reproduce against the pre-D0 (apiVersion 1.2) build. Characterize
> further (does it reproduce with a *smaller* `apiVersion`-dependent entrypoint
> table than 1.4's full one? does a newer/older `libvulkan1` avoid it?) and, if
> confirmed as a loader bug rather than something this ICD's own dispatch-table
> generation can influence, file it upstream rather than attempt a local
> workaround in a component this project does not own
