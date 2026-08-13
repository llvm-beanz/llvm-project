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

Now that we have made it through all the major milestones except performance
tuning, I'd like to work on some wider coverage of feme-run with end-to-end
tests.

Please generate a set of end-to-end tests that start with shaders implemented in
HLSL, compiled to DXIL and SPIRV with Clang, and executed through feme-run to
verify correct execution.

Particular test cases I'd like you to focus on are loops, divergent control
flow, wave operations, barriers, groupshared memory and use cases that combine
all of the above.
