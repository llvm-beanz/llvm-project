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

You left a comment in your thoughts:

> reconstructing a plausible-looking but fake struct type to fill that gap would
> be worse than not raising those kinds at all, since it would silently produce
> a handle type that doesn't match what actually flowed through the real
> frontend.

The logic here is a bit flowed. We need to translate these operations to
something in order to support re-targeting the IR.
