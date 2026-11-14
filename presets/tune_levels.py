#!/usr/bin/env python3
"""Iteratively set master_gain per preset so the category phrase peaks at TARGET dBFS."""
import json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOL = os.path.join(ROOT, "build/VSTForgeRenderTest_artefacts/Release/VSTForgeRenderTest")
TARGET = float(sys.argv[1]) if len(sys.argv) > 1 else -4.5
levels_path = os.path.join(HERE, "levels.json")
levels = json.load(open(levels_path)) if os.path.exists(levels_path) else {}
for it in range(5):
    subprocess.run([sys.executable, os.path.join(HERE, "make_factory_presets.py")], check=True, stdout=subprocess.DEVNULL)
    out = subprocess.run([TOOL, "--tune-presets", os.path.join(HERE, "factory")], check=True, capture_output=True, text=True).stdout
    worst = 0.0
    for line in out.splitlines():
        if not line.startswith("TUNE"): continue
        _, name, cat, peak, rms, gr = line.split("\t")
        peak, gr = float(peak), float(gr)
        g = levels.get(name, -6.0)
        err = TARGET - peak - (1.5 if gr > 0.01 else 0.0)   # limited: overshoot correction
        newg = max(-30.0, min(6.0, round(g + err, 1)))
        worst = max(worst, abs(err) if newg not in (-30.0, 6.0) else 0.0)
        levels[name] = newg
        if it >= 0: print(f"iter {it} {name:18s} {cat:6s} peak {peak:7.2f} rms {float(rms):7.2f} gr {gr:5.2f} gain {g:6.1f} -> {newg:6.1f}")
    json.dump(levels, open(levels_path, "w"), indent=1, sort_keys=True)
    print(f"iter {it}: worst error {worst:.2f} dB")
    if worst < 0.25: break
subprocess.run([sys.executable, os.path.join(HERE, "make_factory_presets.py")], check=True)
