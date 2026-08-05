---
model: gpt-5.6-sol
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

From the previous agent notes:

> - **`struct_buf1` is one instruction away.** Its only difference is a
>   `bitcastI32toF32` we emit and dxilconv does not, because a temp
>   register component this translation typed `float` dxilconv typed
>   `i32`. That is a `inferTempTypes` voting question, not a resource one.

For the struct_buf1 case we don't need this translator to generate
instruction-for-instruction identical outputs to dxilconv's original tests, they
need to be functionally equivalent, so the extra conversion is fine. Please
translate the struct_buf1.ref file into reasonable `CHECK` directives in the asm
file and delete the ref file.

The previous agent notes also showed:

> - **`indexableinput1`/`indexableinput2`** still differ in signature
>   *element numbering*: dxilconv collapses the registers a
>   `dcl_indexrange` spans into one element with `Rows` set to the range's
>   length. That renumbers every element after it, so it is worth
>   measuring against all the signature-carrying fixtures at once.

Please make feme's conversion match dxilconv, and update the tests accordingly
and remove any .ref files you can once the tests are updated.
