---
model: claude-sonnet-5
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

When you build and test ensure that you are using object file caching, and
building with assertions enabled.

When you deviate from the design document please update the design document.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Please implement milestone 5 of the FeMe CPU design:

> 5. **CFG restructurization suite**: the named-shape corpus, the
>    `-verify-structured` postcondition checker, and — now that `feme-run`
>    exists — the generator, its differential harness, and the fuzzer over
>    it. This lands before the linearizer because the linearizer is what
>    starts depending on Phase 1 having actually succeeded.
