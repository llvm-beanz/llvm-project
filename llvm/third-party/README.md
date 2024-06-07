# LLVM Third-Party Dependencies

## BLAKE3

* Source: https://github.com/BLAKE3-team/BLAKE3/commit/454ee5a7c73583cb3060d1464a5d3a4e65f06062
* License: Apache 2.0 With LLVM Exceptions

BLAKE3 is a fast cryptographic hash. LLVM includes BLAKE3 through LLVMSupport
for general use across the codebase as a robust and efficient hash.

> Note: This is grabbed from an arbitrary commit with a few fixes past the
> [1.5.1 tag](https://github.com/BLAKE3-team/BLAKE3/releases/tag/1.5.1). The
> biggest change between this an 1.5.1 is the introduction of the LLVM License
> which simplifies things for LLVM.
