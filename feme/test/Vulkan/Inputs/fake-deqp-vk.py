#!/usr/bin/env python3
# fake-deqp-vk.py - synthetic stand-in for `deqp-vk`, used only by
# run-vulkan-cts.test to exercise run_vulkan_cts.py's batch/recovery/
# verification logic without a real Vulkan device or CTS build.
#
# Reads the same --deqp-caselist-file/--deqp-log-filename arguments
# run_vulkan_cts.py passes to a real deqp-vk, and writes QPA-shaped output
# for a small set of fixed case names, each behaving differently depending
# on how many cases share its invocation (batch size), so a single fixture
# run can exercise: an ordinary pass, a batch that crashes partway through
# (requiring recovery at a finer batch size), a case that fails only under
# contention and passes when re-run alone (the roadmap L145 scenario this
# harness exists to catch), and a case that fails identically either way (a
# real, verified failure).

import argparse
import sys


def read_case_list(path):
    with open(path, encoding="utf-8") as file:
        return [line.strip() for line in file if line.strip()]


def write_result(qpa_file, case_name, status):
    qpa_file.write("#beginTestCaseResult %s\n" % case_name)
    qpa_file.write('<Result StatusCode="%s">ok</Result>\n' % status)
    qpa_file.write("#endTestCaseResult\n")


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--deqp-caselist-file", required=True)
    parser.add_argument("--deqp-log-filename", required=True)
    parser.add_argument("--deqp-shadercache")
    args, _unused = parser.parse_known_args(argv)

    cases = read_case_list(args.deqp_caselist_file)
    alone = len(cases) == 1

    with open(args.deqp_log_filename, "w", encoding="utf-8") as qpa_file:
        for case_name in cases:
            if case_name.endswith(".crash") and not alone:
                # Simulate a process that dies mid-case: the begin marker is
                # written, but the process stops before the matching end
                # marker or any later case in this batch.
                qpa_file.write("#beginTestCaseResult %s\n" % case_name)
                qpa_file.flush()
                sys.exit(1)
            if case_name.endswith(".flaky"):
                write_result(qpa_file, case_name, "Pass" if alone else "Fail")
            elif case_name.endswith(".crash"):
                write_result(qpa_file, case_name, "Pass")
            elif case_name.endswith(".fail"):
                write_result(qpa_file, case_name, "Fail")
            else:
                write_result(qpa_file, case_name, "Pass")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
