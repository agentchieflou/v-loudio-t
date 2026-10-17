#!/usr/bin/env python3
"""Prints the plugins under VSTs/ as a JSON list for the CI matrix.

A plugin is any VSTs/<Name>/CMakeLists.txt. Each entry carries the three
spellings the build uses: {"name": "Reverb", "lower": "reverb",
"upper": "REVERB"}. Deleting a plugin directory removes it from CI with no
other edit (P4, #130).
"""
import json
import pathlib
import sys

root = pathlib.Path(__file__).resolve().parent.parent / "VSTs"
plugins = []
for cmake in sorted(root.glob("*/CMakeLists.txt")):
    name = cmake.parent.name
    plugins.append({"name": name, "lower": name.lower(), "upper": name.upper()})
json.dump(plugins, sys.stdout, separators=(",", ":"))
sys.stdout.write("\n")
