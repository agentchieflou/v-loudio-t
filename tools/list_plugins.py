#!/usr/bin/env python3
"""Prints the plugins under VSTs/ as a JSON list for the CI matrix.

A plugin is any VSTs/<Name>/CMakeLists.txt. Each entry carries the three
spellings the build uses: {"name": "Reverb", "lower": "reverb",
"upper": "REVERB"}, plus "vst3sdk": whether its VST3 is built on the VST3
SDK directly (loudio_add_vst3_plugin, #171) rather than JUCE. Deleting a
plugin directory removes it from CI with no other edit (P4, #130).

--vst3sdk prints only the plugins built on the VST3 SDK.
"""
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent / "VSTs"
plugins = []
for cmake in sorted(root.glob("*/CMakeLists.txt")):
    name = cmake.parent.name
    vst3sdk = "loudio_add_vst3_plugin(" in cmake.read_text(encoding="utf-8")
    if "--vst3sdk" in sys.argv[1:] and not vst3sdk:
        continue
    plugins.append({"name": name, "lower": name.lower(), "upper": name.upper(), "vst3sdk": vst3sdk})
json.dump(plugins, sys.stdout, separators=(",", ":"))
sys.stdout.write("\n")
