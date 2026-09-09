---
model: gpt-5.6-sol
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Verify your changes by building and testing using the
/opt/llvm-tooling/Config.cmake cache file with CMake's -C flag to configure the
build. Test the compiler and runtime support with the targets: check-llvm,
check-clang, check-hlsl-vk and check-hlsl-clang-vk.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository and commit it in its own commit when you're done.

# Request

Can you add tests and confirm that this solution works end-to-end with switch
statements containing breaks and fallthrough embedded inside loops?
