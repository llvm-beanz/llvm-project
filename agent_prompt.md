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

Can you implement R31 from the roadmap document?

> `FeMeGraphics` skeleton: normalized pipeline and prepared-draw descriptions,
> the `feme-render` tool (already specified in Design.md's "Testing Tools" and
> `docs/CommandGuide/feme-render.md`, along with its scene and image fixture
> formats -- only the implementation is left), and the heap YAML image resource
> class (see: §2.6.1)
