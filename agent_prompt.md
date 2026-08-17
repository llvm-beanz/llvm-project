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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

A bunch of the CMake logic around the Vulkan dependencies is more complicated
than it really needs to be. Rather than depending on a CMake-configured
directory of the VulkanHeaders, we should be able to get everything we need from
a Vulkan SDK installation using `find_package(Vulkan)`. That also simplifies
linking against the vulkan loader directory and provides platform agnostic ways
to refer to various files.

Can you update the CMake configuration to be based on finding installed Vulkan
package?
