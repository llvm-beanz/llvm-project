#!/usr/bin/env python3
# A fake stand-in for the real vk_gen_feature_inventory.py, used by
# run-check-feme-capability-manifest.test: writes a fixed, deterministic
# two-row table regardless of its (ignored) vk.xml/version/advertised-file
# arguments, so the test only exercises check-feme-capability-manifest.py's
# own diff logic, not the real generator or a real vk.xml.

import argparse
import sys


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("vk_xml")
    parser.add_argument("--version", action="append", default=[])
    parser.add_argument("--advertised-features")
    parser.add_argument("--advertised-extensions")
    parser.add_argument("-o", "--output", required=True)
    args = parser.parse_args(argv)

    table = (
        "| Category | Version | Name | Advertised | Note |\n"
        "|---|---|---|---|---|\n"
        "| feature | VK_VERSION_1_0 | `fakeFeatureOne` | yes |  |\n"
        "| feature | VK_VERSION_1_0 | `fakeFeatureTwo` | no |  |\n"
    )
    with open(args.output, "w", encoding="utf-8") as f:
        f.write(table)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
