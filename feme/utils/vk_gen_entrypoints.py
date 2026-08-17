#!/usr/bin/env python3
# Generates the FeMe Vulkan ICD's entrypoint table from Vulkan-Headers'
# vk.xml (see "Loader Integration" in feme/docs/FeMeVulkanDesign.md).
#
# Hand-maintaining command names, aliases, and core-version promotions is
# error-prone even when only a subset is implemented, so this reads vk.xml
# itself rather than a checked-in list. It emits one X-macro invocation per
# core Vulkan 1.0/1.1 command:
#
#   FEME_VK_COMMAND(name, dispatch_level)       -- not implemented
#   FEME_VK_COMMAND_IMPL(name, dispatch_level)  -- implemented
#
# `dispatch_level` is one of GLOBAL, INSTANCE, DEVICE, matching how the
# Vulkan loader itself classifies a command (by its first parameter's
# dispatchable handle type, if any). A command name listed in the
# `--implemented` file (one name per line, '#' comments allowed) is emitted
# through `FEME_VK_COMMAND_IMPL` instead of `FEME_VK_COMMAND`, so a caller
# that only defines the latter (mapping every entry to null) still sees
# every command name -- matching "Loader Integration"'s requirement that the
# generated table cover every known command name -- while a caller that also
# defines `FEME_VK_COMMAND_IMPL` (mapping it to a real, distinctly named C++
# symbol) never takes the address of a function that doesn't exist for an
# unimplemented command.
#
# Only the VK_VERSION_1_0 and VK_VERSION_1_1 core features are read.
# Extension commands are out of scope for V0 (see FeMeVulkanDesign.md's
# "Loader Integration": "The driver reports no device extension merely
# because Vulkan-Headers declares it").
#
# Newer vk.xml revisions split each `VK_VERSION_1_x` feature into several
# `VK_{BASE,COMPUTE,GRAPHICS}_VERSION_1_x` features linked by a `depends`
# attribute (e.g. `VK_VERSION_1_0` itself now requires no commands directly;
# it just `depends` on `VK_GRAPHICS_VERSION_1_0`, which in turn `depends` on
# `VK_COMPUTE_VERSION_1_0`, and so on), so a command's containing feature must
# be resolved transitively through `depends` rather than assumed to be a
# direct `<feature name="VK_VERSION_1_x">`.

import argparse
import re
import sys
import xml.etree.ElementTree as ET

CORE_FEATURES = ("VK_VERSION_1_0", "VK_VERSION_1_1")

# First-parameter handle types that make a command dispatched at the
# instance level rather than the device level. VkPhysicalDevice commands are
# dispatched through the instance's table (the loader has no per-physical-
# device dispatch table), matching the Vulkan loader's own convention.
INSTANCE_HANDLE_TYPES = {"VkInstance", "VkPhysicalDevice"}
DEVICE_HANDLE_TYPES = {"VkDevice", "VkQueue", "VkCommandBuffer"}


def resolve_dependent_features(name, features_by_name, resolved=None):
    """Returns the transitive closure of `name` and every feature it
    (recursively) `depends` on, per `features_by_name` (name -> <feature>).

    A `depends` attribute is a boolean expression combining feature/extension
    names with `+` (AND) and `,` (OR), optionally parenthesized; every name it
    mentions is walked regardless of which operator joins it; because core
    version features never gate a `<command>` requirement behind an OR'd
    extension (see the `depends` note above `CORE_FEATURES`), this closure is
    exactly the set of sub-features a core version is built from.
    """
    if resolved is None:
        resolved = set()
    if name in resolved or name not in features_by_name:
        return resolved
    resolved.add(name)
    depends = features_by_name[name].get("depends")
    if depends:
        for dependency in re.findall(r"[A-Za-z0-9_]+", depends):
            resolve_dependent_features(dependency, features_by_name, resolved)
    return resolved


def parse_commands(vk_xml_path):
    """Returns {command_name: dispatch_level} for every core 1.0/1.1 command,
    resolving `alias` commands to their target's dispatch level."""
    tree = ET.parse(vk_xml_path)
    root = tree.getroot()

    # Map every <command>, keyed by name, to either its first parameter's
    # type (a proto command) or its alias target (an alias command).
    protos = {}
    aliases = {}
    for command in root.findall("./commands/command"):
        name = command.get("name")
        alias = command.get("alias")
        if alias:
            aliases[name] = alias
            continue
        proto = command.find("proto/name")
        name = proto.text
        first_param = command.find("param/type")
        protos[name] = first_param.text if first_param is not None else None

    def dispatch_level(name):
        seen = set()
        while name in aliases:
            if name in seen:
                raise ValueError(f"alias cycle involving {name}")
            seen.add(name)
            name = aliases[name]
        handle_type = protos.get(name)
        if handle_type in INSTANCE_HANDLE_TYPES:
            return "INSTANCE"
        if handle_type in DEVICE_HANDLE_TYPES:
            return "DEVICE"
        return "GLOBAL"

    features_by_name = {
        feature.get("name"): feature for feature in root.findall("./feature")
    }
    core_feature_names = set()
    for feature_name in CORE_FEATURES:
        core_feature_names |= resolve_dependent_features(feature_name, features_by_name)

    commands = {}
    for feature_name in core_feature_names:
        feature = features_by_name[feature_name]
        api = feature.get("api", "")
        if "vulkan" not in api.split(","):
            continue
        for require in feature.findall("require"):
            # A `<require depends="...">` conditions its contents on an
            # optional extension or struct field rather than the core
            # version itself; core versions never gate a `<command>` this
            # way (see the note above `CORE_FEATURES`), but skip such blocks
            # defensively rather than assume that always holds.
            if require.get("depends"):
                continue
            for command_ref in require.findall("command"):
                name = command_ref.get("name")
                commands[name] = dispatch_level(name)
    return commands


def read_implemented(path):
    if not path:
        return set()
    names = set()
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                names.add(line)
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vk_xml", help="Path to Vulkan-Headers' vk.xml")
    parser.add_argument("output", help="Path to the generated .inc file")
    parser.add_argument(
        "--implemented",
        help="Path to a file listing implemented command names, one per line",
    )
    args = parser.parse_args()

    commands = parse_commands(args.vk_xml)
    implemented = read_implemented(args.implemented)
    unknown_implemented = implemented - set(commands)
    if unknown_implemented:
        sys.exit(
            "vk_gen_entrypoints.py: --implemented lists commands that are "
            "not core Vulkan 1.0/1.1 entrypoints: "
            + ", ".join(sorted(unknown_implemented))
        )

    with open(args.output, "w") as out:
        out.write(
            "// Generated by feme/utils/vk_gen_entrypoints.py from vk.xml. "
            "Do not edit.\n"
            "#ifndef FEME_VK_COMMAND\n"
            '#error "FEME_VK_COMMAND must be defined before including this '
            'file"\n'
            "#endif\n"
            "#ifndef FEME_VK_COMMAND_IMPL\n"
            "#define FEME_VK_COMMAND_IMPL(name, level) FEME_VK_COMMAND(name, "
            "level)\n"
            "#define FEME_VK_GEN_UNDEF_COMMAND_IMPL\n"
            "#endif\n"
        )
        for name in sorted(commands):
            macro = "FEME_VK_COMMAND_IMPL" if name in implemented else "FEME_VK_COMMAND"
            out.write(f"{macro}({name}, {commands[name]})\n")
        out.write(
            "#undef FEME_VK_COMMAND\n"
            "#ifdef FEME_VK_GEN_UNDEF_COMMAND_IMPL\n"
            "#undef FEME_VK_COMMAND_IMPL\n"
            "#undef FEME_VK_GEN_UNDEF_COMMAND_IMPL\n"
            "#endif\n"
        )


if __name__ == "__main__":
    main()
