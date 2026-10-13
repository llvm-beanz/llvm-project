#!/usr/bin/env python3
# A `deqp-vk` test double for `run-vulkan-cts-devicelost-timedout.test`
# (roadmap L146). Reads `--deqp-caselist-file`, writes a QPA record to
# `--deqp-log-filename` for each case, driven entirely by the case name:
#
#   *.plain_pass          always Pass.
#   *.devicelost_flaky     DeviceLost the first time it is asked about this
#                          case, Pass every time after (simulates
#                          contention-only DeviceLost).
#   *.devicelost_persistent
#                          DeviceLost every time (simulates a real,
#                          reproducible device loss).
#   *.hangs                sleeps past any reasonable timeout and writes no
#                          record at all (simulates a stuck case).
#
# Per-case call counts are persisted as files under $FAKE_DEQP_STATE_DIR so
# behavior is consistent across this script's several separate invocations
# within one `run_vulkan_cts.py` run (initial round, recovery round,
# verification round).

import os
import re
import sys
import time

BEGIN = re.compile(r"^--deqp-caselist-file=(.+)$")
LOG = re.compile(r"^--deqp-log-filename=(.+)$")


def call_count(case_name):
    state_dir = os.environ["FAKE_DEQP_STATE_DIR"]
    os.makedirs(state_dir, exist_ok=True)
    path = os.path.join(state_dir, case_name)
    count = 0
    if os.path.exists(path):
        with open(path, encoding="utf-8") as file:
            count = int(file.read().strip() or "0")
    count += 1
    with open(path, "w", encoding="utf-8") as file:
        file.write(str(count))
    return count


def status_for(case_name):
    if case_name.endswith(".plain_pass"):
        return "Pass"
    if case_name.endswith(".devicelost_flaky"):
        return "DeviceLost" if call_count(case_name) == 1 else "Pass"
    if case_name.endswith(".devicelost_persistent"):
        call_count(case_name)
        return "DeviceLost"
    if case_name.endswith(".hangs"):
        time.sleep(5)
        return None
    raise ValueError("unrecognized fake case name: %s" % case_name)


def main(argv):
    case_list_path = None
    qpa_path = None
    for arg in argv:
        match = BEGIN.match(arg)
        if match:
            case_list_path = match.group(1)
            continue
        match = LOG.match(arg)
        if match:
            qpa_path = match.group(1)
    assert case_list_path and qpa_path

    with open(case_list_path, encoding="utf-8") as file:
        case_names = [line.strip() for line in file if line.strip()]

    with open(qpa_path, "w", encoding="utf-8") as qpa_file:
        qpa_file.write("#beginSession\n")
        for case_name in case_names:
            status = status_for(case_name)
            if status is None:
                # Simulates a case that never completes: write nothing for
                # it (and stop -- everything after a hang in a real batch
                # is unrun too).
                break
            qpa_file.write("#beginTestCaseResult %s\n" % case_name)
            qpa_file.write("<TestCaseResult>\n")
            qpa_file.write(
                ' <Result StatusCode="%s">%s</Result>\n' % (status, status))
            qpa_file.write("</TestCaseResult>\n")
            qpa_file.write("#endTestCaseResult\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
