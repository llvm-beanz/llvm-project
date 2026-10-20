#!/usr/bin/env python3
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, sys.argv.pop())
import run_vulkan_cts


class VerificationIsolationTest(unittest.TestCase):
    def test_parallel_failures_are_verified_sequentially(self):
        initial = {"dEQP-VK.x.fail": "Fail", "dEQP-VK.x.lost": "DeviceLost"}
        verified = {case: "Pass" for case in initial}
        with tempfile.TemporaryDirectory() as directory:
            case_list = Path(directory) / "cases.txt"
            case_list.write_text("\n".join(initial), encoding="utf-8")
            with mock.patch.object(
                run_vulkan_cts, "run_round",
                side_effect=[(initial, [], []), (verified, [], [])],
            ) as rounds:
                self.assertEqual(run_vulkan_cts.main([
                    "--deqp-vk", "unused", "--case-list", str(case_list),
                    "--cwd", directory, "--output-dir", directory,
                    "--workers", "6",
                ]), 0)
            self.assertEqual(rounds.call_count, 2)
            self.assertEqual(rounds.call_args_list[0].args[4], 6)
            verification = rounds.call_args_list[1].args
            self.assertEqual(verification[3:5], ("verify", 1))
            self.assertEqual(
                verification[1], [[case] for case in sorted(initial)])
            report = (Path(directory) / "report.txt").read_text(encoding="utf-8")
            self.assertIn("  Pass: 2\n", report)
            self.assertNotIn("  Fail:", report)
            self.assertNotIn("  DeviceLost:", report)


if __name__ == "__main__":
    unittest.main()
