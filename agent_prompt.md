---
model: claude-opus-5
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

Let's iterate on the FemeCPUDesign.md design.

1. One root constant buffer is fine (at least initially), but we should document
   the limitation and how it compares to GPU APIs.
2. Yes, it probably makes sense to have bounds checking be controllable
   per-descriptor.
3. Use masked intrinsics to represent the masks between phases so they are
   testable.
4. Let's talk more about the possible designs for representing descriptor
   formats.
5. I agree with the assessments about graphics support in the "Decisions made
   now to keep it cheap later" section.
6. FeMe will need to grow a test suite for CFC restructuring.
