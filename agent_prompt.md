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

Now that we have a finalized design for the FeMe CPU target.

Please implement the first step on the roadmap:

> 1. **Scaffolding + raised-IR contract + ABI header**:
>   `Target/CPU/RuntimeABI.h`, wave size resolution (`--wave-size` in
>   `DriverOptions`, shader declaration, host default) with its diagnostics,
>   empty passes registered in `feme-opt`, and front-end raising for the
>   descriptor-heap, barrier and wave operations required by the first
>   executable milestones. Unsupported raised operations get an early CPU
>   target diagnostic.
