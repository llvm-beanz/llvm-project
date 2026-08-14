#!/usr/bin/env python3
# feme-wave-size-sweep.py - Runs feme-run at several wave sizes, checking
# each against the same FileCheck input.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Roadmap step R1's other §2.4.1 prerequisite (see feme/docs/Roadmap.md):
# a wave-size-independent HLSL end-to-end test (e.g.
# feme/test/Tools/feme-run/HLSL/loop.hlsl) should run at `W` in {4, 8, 16,
# 32} and produce the same, already-`FileCheck`ed output every time, so a
# widening bug that only shows up at a wave size other than the tree's
# overwhelmingly common `W = 4` is caught. This script is the "one word
# instead of four `RUN:` lines" `%feme-run` substitution wraps: it runs
# `feme-run` once per `--wave-sizes` entry and `FileCheck`s each run's
# output against the same `--check-file`, rather than a test spelling out
# one `feme-run | FileCheck` pipeline per wave size by hand.
#
# Usage:
#   feme-wave-size-sweep.py --feme-run=<path> --filecheck=<path>
#       --check-file=<path> --wave-sizes=4,8,16,32
#       -- <feme-run arguments, excluding --wave-size>

import argparse
import subprocess
import sys


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--feme-run", required=True,
                        help="path to the feme-run executable")
    parser.add_argument("--filecheck", required=True,
                        help="path to the FileCheck executable")
    parser.add_argument("--check-file", required=True,
                        help="the file FileCheck reads its CHECK lines from")
    parser.add_argument("--wave-sizes", default="4",
                        help="comma-separated feme-run --wave-size values "
                             "(default: 4)")
    parser.add_argument("feme_run_args", nargs=argparse.REMAINDER,
                        help="feme-run's own arguments, after a '--'")
    args = parser.parse_args(argv)
    if args.feme_run_args and args.feme_run_args[0] == "--":
        args.feme_run_args = args.feme_run_args[1:]
    return args


def main(argv):
    args = parse_args(argv)
    wave_sizes = [int(w) for w in args.wave_sizes.split(",") if w]

    for wave_size in wave_sizes:
        run_proc = subprocess.run(
            [args.feme_run, "--wave-size=%d" % wave_size]
            + args.feme_run_args,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if run_proc.returncode != 0:
            sys.stderr.write(
                "feme-wave-size-sweep: feme-run failed at wave-size=%d "
                "(exit %d):\n%s\n"
                % (wave_size, run_proc.returncode,
                   run_proc.stderr.decode()))
            return 1

        check_proc = subprocess.run(
            [args.filecheck, args.check_file], input=run_proc.stdout,
            stderr=subprocess.PIPE)
        if check_proc.returncode != 0:
            sys.stderr.write(
                "feme-wave-size-sweep: FileCheck failed at wave-size=%d:\n"
                "%s\n" % (wave_size, check_proc.stderr.decode()))
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
