#!/usr/bin/env python3
"""Enumerates Vulkan's mandatory 1.3/1.4 feature/limit/extension surface
straight from vk.xml, and cross-checks it against what this ICD actually
advertises (see roadmap D1, "An accurate 1.3/1.4 mandatory-feature/limit/
extension inventory", in feme/docs/Roadmap.md).

Once a core version's own aggregate struct
(`VkPhysicalDeviceVulkan{13,14}Features`/`...Properties`) exists, a
conformant implementation of that version must report every one of its
struct members truthfully, and must implement every extension `vk.xml`
records as `promotedto` that version -- this is the audit "measure
honestly" turned into a checklist, so it is derived the same way
`vk_gen_entrypoints.py` already resolves `CORE_FEATURES`: read directly out
of vk.xml rather than re-derived by hand (which is what roadmap C6 did for
1.2's much shorter list).

Usage:
    vk_gen_feature_inventory.py <vk.xml> [--version VK_VERSION_1_3 ...]
        [--advertised-features <file>] [--advertised-extensions <file>]
        [-o <output.md>]

`--advertised-features`/`--advertised-extensions` each name a file listing
one `name` or `name = note` per line ('#' comments allowed) that this
ICD's `PhysicalDeviceInfo.cpp`/`getSupportedDeviceExtensions` already
advertises as supported; every enumerated feature/extension is checked
off against it, with the optional note surfaced in the checklist's own
"Note" column (e.g. to record that a feature is advertised only through
its pre-promotion struct, not yet the aggregate one). Limits have no
advertised-file equivalent: unlike a feature bit or an extension, a limit's
"is this honest" question depends on the field's numeric value, not a
name, so this script only enumerates the limit fields a version's promoted
`...Properties` struct requires -- it does not judge them.
"""

import argparse
import xml.etree.ElementTree as ET

# The struct/property members every `VkPhysicalDeviceVulkan{13,14}Features`/
# `...Properties` type declares before its first real field -- present in
# every Vulkan structure, never a feature or limit in their own right.
STRUCT_HEADER_MEMBERS = {"sType", "pNext"}


def struct_members(root, type_name):
    """Returns the ordered list of member names `type_name` (e.g.
    "VkPhysicalDeviceVulkan13Features") declares in vk.xml, excluding
    `STRUCT_HEADER_MEMBERS`."""
    struct_type = root.find(f"./types/type[@name='{type_name}']")
    if struct_type is None:
        raise ValueError(f"{type_name} is not declared in vk.xml")
    members = []
    for member in struct_type.findall("member"):
        name = member.find("name")
        if name is not None and name.text not in STRUCT_HEADER_MEMBERS:
            members.append(name.text)
    return members


def promoted_extensions(root, version):
    """Returns the sorted list of extension names vk.xml records as
    `promotedto="<version>"` (e.g. "VK_VERSION_1_3"), i.e. every extension
    folded into that version's core, without which claiming the version is
    not a truthful claim regardless of its own aggregate feature struct."""
    extensions = [
        extension.get("name")
        for extension in root.findall("./extensions/extension")
        if extension.get("promotedto") == version
    ]
    return sorted(extensions)


def read_names(path):
    """Reads a `--advertised-features`/`--advertised-extensions` file: one
    `name` or `name = note` per line ('#' comments allowed). Returns
    {name: note}, with note "" when the line carries none -- e.g.
    `dynamicRendering`'s own line records *how* it's advertised (through its
    pre-promotion VK_KHR_dynamic_rendering path, not yet through the
    aggregate Vulkan13Features struct itself), which the checklist surfaces
    instead of silently collapsing to a bare "yes"."""
    if not path:
        return {}
    names = {}
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            name, _, note = line.partition("=")
            names[name.strip()] = note.strip()
    return names


def version_suffix(version):
    """"VK_VERSION_1_3" -> "13", matching the aggregate struct names'
    own `Vulkan13Features`/`Vulkan13Properties` spelling."""
    major, minor = version.split("_")[-2:]
    return major + minor


def build_inventory(vk_xml_path, versions):
    """Returns a list of (category, version, name) tuples: one per
    mandatory feature-struct member, limit-struct member, and promoted
    extension, for every version in `versions`."""
    tree = ET.parse(vk_xml_path)
    root = tree.getroot()

    inventory = []
    for version in versions:
        suffix = version_suffix(version)
        for name in struct_members(root, f"VkPhysicalDeviceVulkan{suffix}Features"):
            inventory.append(("feature", version, name))
        for name in struct_members(
            root, f"VkPhysicalDeviceVulkan{suffix}Properties"
        ):
            inventory.append(("limit", version, name))
        for name in promoted_extensions(root, version):
            inventory.append(("extension", version, name))
    return inventory


def render(inventory, advertised_features, advertised_extensions):
    lines = [
        "| Category | Version | Name | Advertised | Note |",
        "|---|---|---|---|---|",
    ]
    for category, version, name in inventory:
        note = ""
        if category == "limit":
            advertised = "n/a"
        elif category == "feature":
            advertised = "yes" if name in advertised_features else "no"
            note = advertised_features.get(name, "")
        else:
            advertised = "yes" if name in advertised_extensions else "no"
            note = advertised_extensions.get(name, "")
        lines.append(f"| {category} | {version} | `{name}` | {advertised} | {note} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vk_xml", help="Path to Vulkan-Headers' vk.xml")
    parser.add_argument(
        "--version",
        dest="versions",
        action="append",
        default=[],
        help="A VK_VERSION_1_x feature name to inventory (repeatable); "
        "defaults to VK_VERSION_1_3 and VK_VERSION_1_4",
    )
    parser.add_argument(
        "--advertised-features",
        help="Path to a file listing feature-struct member names this ICD "
        "already advertises as VK_TRUE, one per line",
    )
    parser.add_argument(
        "--advertised-extensions",
        help="Path to a file listing device extension names this ICD "
        "already advertises, one per line",
    )
    parser.add_argument(
        "-o",
        "--output",
        help="Path to write the generated Markdown checklist to (defaults "
        "to stdout)",
    )
    args = parser.parse_args()

    versions = args.versions or ["VK_VERSION_1_3", "VK_VERSION_1_4"]
    inventory = build_inventory(args.vk_xml, versions)
    advertised_features = read_names(args.advertised_features)
    advertised_extensions = read_names(args.advertised_extensions)

    unknown_features = set(advertised_features) - {
        name for category, _, name in inventory if category == "feature"
    }
    if unknown_features:
        raise SystemExit(
            "vk_gen_feature_inventory.py: --advertised-features lists names "
            "that are not members of any inventoried version's feature "
            "struct: " + ", ".join(sorted(unknown_features))
        )
    unknown_extensions = set(advertised_extensions) - {
        name for category, _, name in inventory if category == "extension"
    }
    if unknown_extensions:
        raise SystemExit(
            "vk_gen_feature_inventory.py: --advertised-extensions lists "
            "names that are not promoted into any inventoried version: "
            + ", ".join(sorted(unknown_extensions))
        )

    output = render(inventory, advertised_features, advertised_extensions)
    if args.output:
        with open(args.output, "w") as f:
            f.write(output)
    else:
        print(output, end="")


if __name__ == "__main__":
    main()
