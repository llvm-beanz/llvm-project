#!/usr/bin/env python3
# check-feme-shared-core.py - Fast, named-failure regression gate for
# FeMe's shared-core libraries (FeMeImage, FeMeExecution, ...).
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# WARP roadmap WS3 ("Establish shared-core regression gates ... Move
# portable tests with their helpers, retaining loader-level tests in
# Vulkan"): every WS0-WS2 extraction already added its new portable
# logic's tests directly to a shared-core gtest binary (FeMeImageTests,
# FeMeExecutionTests) rather than only exercising it indirectly through
# `FeMeVulkanTests`/CTS -- so the "move portable tests" half of WS3's
# text was, in effect, already done incrementally, one extraction at a
# time. What was missing is a *fast, scoped* way to run just those
# binaries and see which named test failed, rather than either running
# the full `check-feme` lit suite (which discovers thousands of
# unrelated tests across the whole project) or trusting only an
# aggregate pass count. This script is the `check-feme-shared-core`
# CMake target's driver: run every given shared-core gtest binary
# directly, and report a per-suite summary naming any failing test, not
# just a pass/fail total -- the roadmap's own exit-evidence text for WS3
# ("compare named failures, not only pass totals").
#
# This is a fast, local pre-flight gate, not a replacement for
# `check-feme` or a bounded CTS run: it only proves the shared-core
# libraries' own unit tests still pass, nothing about Vulkan/D3D
# integration, command recording, or device-level behavior.
#
# Usage:
#   check-feme-shared-core.py --gtest-binary=<path1> --gtest-binary=<path2> ...

import argparse
import re
import subprocess
import sys

# A gtest binary's own "[  FAILED  ] Suite.Case" lines (its final summary
# section lists each failing test by name, one per line, prefixed with
# this exact six-space-padded bracket). Matching this instead of just the
# process's own exit code is what lets this script name each failure
# rather than only report a binary-level pass/fail total.
FAILED_LINE_RE = re.compile(r"^\[\s+FAILED\s+\]\s+(\S+)")


def run_gtest_binary(binary):
    """Runs one gtest binary and returns (succeeded, named_failures,
    output). `named_failures` is the list of "Suite.Case" strings the
    binary's own summary reported as failed; it can be empty even when
    `succeeded` is False if the binary crashed before printing a normal
    gtest summary (e.g. a segfault) -- that case is still reported, just
    without named failures to list."""
    proc = subprocess.run([binary], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    output = proc.stdout.decode(errors="replace")
    failures = [m.group(1) for line in output.splitlines()
               if (m := FAILED_LINE_RE.match(line))]
    return proc.returncode == 0, failures, output


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gtest-binary", action="append", default=[],
                        dest="binaries", metavar="PATH",
                        help="a shared-core gtest binary to run, may be "
                             "repeated")
    args = parser.parse_args(argv)

    if not args.binaries:
        sys.stderr.write(
            "check-feme-shared-core: no --gtest-binary=PATH arguments "
            "given\n")
        return 1

    any_failed = False
    for binary in args.binaries:
        print("check-feme-shared-core: running %s" % binary)
        succeeded, failures, output = run_gtest_binary(binary)
        if succeeded:
            print("check-feme-shared-core: %s: all tests passed" % binary)
            continue
        any_failed = True
        if failures:
            sys.stderr.write(
                "check-feme-shared-core: %s: %d named failure(s):\n  %s\n"
                % (binary, len(failures), "\n  ".join(failures)))
        else:
            sys.stderr.write(
                "check-feme-shared-core: %s: failed with no gtest "
                "summary (crashed?); full output:\n%s\n"
                % (binary, output))

    if any_failed:
        return 1
    print("check-feme-shared-core: all %d binary(ies) passed"
         % len(args.binaries))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
