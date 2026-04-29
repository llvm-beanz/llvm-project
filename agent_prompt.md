# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests covering each phase of translation in the compiler, and that your changes conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Verify your changes by building and testing using the /opt/llvm-tooling/Config.cmake cache file with CMake's -C flag to configure the build. Test the compiler and runtime support with the targets: check-llvm, check-clang, check-hlsl-vk and check-hlsl-clang-vk.

Break your changes into small code changes, with each change committed separately. Record your thought process into a file named "agent_thoughts.md" at the root of the repository and commit it in its own commit when you're done.

# Request

Can you implement the QuadReadLaneAt function for HLSL as referenced in this issue (https://github.com/llvm/llvm-project/issues/99174)?

Before starting the Clang changes, generate test cases for the new intrinsics in the offload-test-suite repository at ~/dev/offload-test-suite. You can verify that they work correctly with DXC by building the check-hlsl-vk target. You should do this before making any changes to the clang or LLVM code.

Once you have working tests, add support to Clang and LLVM lowering the changes through to the DirectX and SPIRV backends.
