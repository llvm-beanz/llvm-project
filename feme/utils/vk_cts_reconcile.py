#!/usr/bin/env python3
# vk_cts_reconcile.py - Reconciles crash-tolerant Vulkan CTS QPA results.
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM
# Exceptions. See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# A deqp-vk process can exit after beginning a case but before it writes that
# case's result. Aggregate process exit codes and result totals consequently
# cannot tell a completed run from a partial one. This utility reconstructs
# the completed per-case set from QPA records, reports the requested cases
# that remain unrun, and compares complete result maps between runs.

"""Reconciles crash-tolerant Vulkan CTS QPA result files.

Usage:
  vk_cts_reconcile.py --case-list cases.txt --qpa attempt-0.qpa [--qpa ...]
      [--baseline-qpa baseline.qpa ...] [--expected-failures failures.txt]
      [-o report.txt]

Each `--qpa` may be repeated. A QPA record only counts after its matching
`#endTestCaseResult` marker, so a process that stops mid-case leaves that case
in the report's deterministic `Unrun cases` section. Repeated completed
results for the same case must agree; disagreement is a hard error instead of
silently selecting one attempt.

`--baseline-qpa` compares actual per-case statuses, not aggregate counts.
`--expected-failures` is a plain list of case names whose `Fail` result is
expected; an unexpected failure is reported separately. Blank lines and `#`
comments in input lists are ignored.
"""

import argparse
import re
import sys
from collections import Counter


BEGIN_RECORD = re.compile(r"^#beginTestCaseResult (.+)$")
END_RECORD = "#endTestCaseResult"
STATUS = re.compile(r'<Result StatusCode="([^"]+)"')


def read_case_list(path):
    with open(path, encoding="utf-8") as file:
        return [
            line.split("#", 1)[0].strip()
            for line in file
            if line.split("#", 1)[0].strip()
        ]


def read_qpa(path):
    results = {}
    case_name = None
    record = []
    with open(path, encoding="utf-8", errors="replace") as file:
        for line in file:
            line = line.rstrip("\n")
            begin = BEGIN_RECORD.match(line)
            if begin:
                case_name = begin.group(1)
                record = []
                continue
            if case_name is None:
                continue
            if line == END_RECORD:
                statuses = STATUS.findall("\n".join(record))
                if len(statuses) != 1:
                    raise ValueError(
                        "%s: completed case %s has %d result statuses"
                        % (path, case_name, len(statuses)))
                previous = results.get(case_name)
                if previous is not None and previous != statuses[0]:
                    raise ValueError(
                        "%s: case %s has conflicting statuses %s and %s"
                        % (path, case_name, previous, statuses[0]))
                results[case_name] = statuses[0]
                case_name = None
                continue
            record.append(line)
    return results


def merge_results(paths, label):
    merged = {}
    for path in paths:
        for case_name, status in read_qpa(path).items():
            previous = merged.get(case_name)
            if previous is not None and previous != status:
                raise ValueError(
                    "%s: case %s has conflicting statuses %s and %s"
                    % (label, case_name, previous, status))
            merged[case_name] = status
    return merged


def format_report(cases, results, baseline, expected_failures):
    case_set = set(cases)
    unknown = sorted(set(results) - case_set)
    if unknown:
        raise ValueError(
            "QPA result(s) are not in --case-list: %s" % ", ".join(unknown))
    unknown_expected = sorted(expected_failures - case_set)
    if unknown_expected:
        raise ValueError(
            "expected failure(s) are not in --case-list: %s"
            % ", ".join(unknown_expected))

    counts = Counter(results.values())
    unrun = [case_name for case_name in cases if case_name not in results]
    changed = [
        case_name for case_name in cases
        if baseline.get(case_name) != results.get(case_name)
    ]
    unexpected_failures = sorted(
        case_name for case_name, status in results.items()
        if status == "Fail" and case_name not in expected_failures)

    lines = [
        "Total cases: %d" % len(cases),
        "Completed cases: %d" % len(results),
        "Unrun cases: %d" % len(unrun),
        "Changed cases: %d" % len(changed),
        "Unexpected failures: %d" % len(unexpected_failures),
        "Result counts:",
    ]
    lines.extend("  %s: %d" % (status, counts[status])
                 for status in sorted(counts))
    if unrun:
        lines.extend(["Unrun case names:"] + ["  %s" % case for case in unrun])
    if changed:
        lines.extend(
            ["Changed case names:"]
            + ["  %s: %s -> %s" % (
                case, baseline.get(case, "Unrun"), results.get(case, "Unrun"))
               for case in changed])
    if unexpected_failures:
        lines.extend(["Unexpected failure names:"]
                     + ["  %s" % case for case in unexpected_failures])
    return "\n".join(lines) + "\n"


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case-list", required=True,
                        help="plain-text list of requested dEQP case names")
    parser.add_argument("--qpa", action="append", required=True,
                        help="QPA result file from the current run")
    parser.add_argument("--baseline-qpa", action="append", default=[],
                        help="QPA result file from the baseline run")
    parser.add_argument("--expected-failures",
                        help="case names whose Fail result is expected")
    parser.add_argument("-o", "--output",
                        help="write the reconciliation report here")
    args = parser.parse_args(argv)

    try:
        cases = read_case_list(args.case_list)
        if len(cases) != len(set(cases)):
            raise ValueError("--case-list contains duplicate case names")
        results = merge_results(args.qpa, "current QPA input")
        baseline = merge_results(args.baseline_qpa, "baseline QPA input")
        expected_failures = set(read_case_list(args.expected_failures)
                                if args.expected_failures else [])
        report = format_report(cases, results, baseline, expected_failures)
    except (OSError, ValueError) as error:
        sys.stderr.write("vk_cts_reconcile: %s\n" % error)
        return 1

    if args.output:
        with open(args.output, "w", encoding="utf-8") as file:
            file.write(report)
    else:
        print(report, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
