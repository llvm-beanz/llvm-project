---
model: claude-opus-5
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

Can you flesh out the R30 and V5 gaps from the roadmap document?

The previous agent left the comment:

> - Real shader-side image/sampler consumption: still blocked on R30's
>   remaining compiler-side reflection work, as scoped above. This is the
>   one gap this follow-up pass did not attempt, and it is the largest one
>   by far -- closing it is its own multi-step project, not a small addition
>   to this session's four smaller fixes.
