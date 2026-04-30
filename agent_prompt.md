# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests covering each phase of translation in the compiler, and that your changes conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Verify your changes by building and testing using the /opt/llvm-tooling/Config.cmake cache file with CMake's -C flag to configure the build. Test the compiler and runtime support with the targets: check-llvm, check-clang, check-hlsl-vk and check-hlsl-clang-vk.

Break your changes into small code changes, with each change committed separately. Record your thought process into a file named "agent_thoughts.md" at the root of the repository and commit it in its own commit when you're done.

# Request

The tests you've added in the offload-test-suite do not cover all the supported overloads for QuadReadLaneAt. DXC supports arithmetic types of varying sizes as well as boolean types and user-defined structures. Please add complete test coverage in the offload-test-suite, LLVM and Clang and address any issues that arise.
