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
the root of the repository and commit it in its own commit when you're done.

# Request

I'd like you to build out the `dxbc-as` tool described in the design document.
While this is a testing tool so it doesn't need to be fully production-quality I
would like it to be well engineered.

Specifically I'd like to see the structure follow traditional compiler design,
lexing, parsing, and building out a stack of instructions which then get dumped
either to binary or text.

I'd also like you to build a fuzzing frontend for it so that we can ensure that
it can handle a wide array of inputs gracefully.
