"""Checks that every parameter ID used by a factory preset is declared by the processor.

A typo in a preset ID is otherwise silently ignored. Run from the repository root:
    python Tests/check_presets.py
"""
import re
import sys

processor = open("Source/PluginProcessor.cpp", encoding="utf-8").read()
declared = set(re.findall(r'add(?:Float|Knob|Switch) \(layout, "(\w+)"', processor))
declared |= set(re.findall(r'ParameterID \{ "(\w+)", 1 \}', processor))
# IDs registered in a loop from a string array, e.g. the EQ bands.
for array in re.findall(r'const char\* ids\[\] \{([^}]*)\}', processor):
    declared |= set(re.findall(r'"(\w+)"', array))

presets = open("Source/PresetManager.cpp", encoding="utf-8").read()
used = set(re.findall(r'\{ "(\w+)", -?[\d.]+f? \}', presets))
names = re.findall(r'^\s+\{ "([A-Z0-9][\w ]+)",\s*$', presets, re.M)

unknown = sorted(used - declared)
print("factory presets:", len(names) + 1)  # + "Init", which has no values
print("unknown preset ids:", unknown or "none")
sys.exit(1 if unknown else 0)
