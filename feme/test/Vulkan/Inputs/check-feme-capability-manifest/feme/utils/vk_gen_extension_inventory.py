#!/usr/bin/env python3
# A fake stand-in for the real vk_gen_extension_inventory.py, used by
# run-check-feme-capability-manifest.test: writes a fixed, deterministic
# two-row table regardless of its (ignored) vk.xml/advertised-file
# arguments, so the test only exercises check-feme-capability-manifest.py's
# own diff logic, not the real generator or a real vk.xml.

import argparse
import sys


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("vk_xml")
    parser.add_argument("--advertised", required=True)
    parser.add_argument("--core-promoted", required=True)
    parser.add_argument("--planned", required=True)
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args(argv)

    # Deliberately differs from the committed fixture doc's second row
    # ("Not implemented" there vs. "Advertised" here) so this test's
    # extension-inventory half exercises the "drifted" path, while the
    # feature-inventory half (same fixed table both sides) exercises the
    # "unchanged" path -- one test covers both outcomes.
    table = (
        "| Extension | Status | Note |\n"
        "|---|---|---|\n"
        "| `VK_FAKE_extension_one` | Advertised |  |\n"
        "| `VK_FAKE_extension_two` | Advertised |  |\n"
    )
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(table)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
