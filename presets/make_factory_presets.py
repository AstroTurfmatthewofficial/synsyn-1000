#!/usr/bin/env python3
"""Generate the factory bank (presets/factory/<Category>/<Name>.vfpreset).

Each preset lists only the parameters that differ from the defaults; the
render tool's --normalise-presets mode then rewrites the files as full APVTS
state (same format as user presets). master_gain comes from levels.json,
which --tune-presets output is used to update (peak target ~ -4.5 dBFS).

Enum indices: wave 0 Sine 1 Saw 2 Square 3 Triangle 4 Noise;
filter 0 LP 1 HP 2 BP; LFO wave 0 Sine 1 Tri 2 SawUp 3 SawDn 4 Square 5 S&H;
LFO target 0 None 1 Cutoff 2 Reso 3 Pitch 4-6 Pitch1-3 7-9 Level1-3 10 Amp;
divisions 6 1/4, 7 1/4D, 9 1/8, 10 1/8D, 11 1/8T, 12 1/16, 13 1/16D.
"""
import json, os, sys
from xml.sax.saxutils import quoteattr

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "factory")

def osc(n, wave, level=0.8, coarse=0, fine=0.0, on=1):
    p = f"osc{n}_"
    return {p+"on": on, p+"wave": wave, p+"level": level, p+"coarse": coarse, p+"fine": fine}

def amp(a, d, s, r):  return {"amp_attack": a, "amp_decay": d, "amp_sustain": s, "amp_release": r}
def fenv(a, d, s, r): return {"fenv_attack": a, "fenv_decay": d, "fenv_sustain": s, "fenv_release": r}
def filt(t, cutoff, res=0.0, key=0.0, env=0.0):
    return {"filter_type": t, "filter_cutoff": cutoff, "filter_res": res, "filter_keytrack": key, "filter_envamt": env}
def lfo(n, wave, target, amount, rate=2.0, sync=0, div=6, depth=1.0, mode=0, delay=0.0, phase=0.0):
    p = f"lfo{n}_"
    return {p+"wave": wave, p+"target": target, p+"amount": amount, p+"rate": rate, p+"sync": sync, p+"div": div,
            p+"depth": depth, p+"mode": mode, p+"delay": delay, p+"phase": phase}
def chorus(rate=0.6, depth=0.5, mix=0.4): return {"chorus_on": 1, "chorus_rate": rate, "chorus_depth": depth, "chorus_mix": mix}
def delay(time=375.0, fb=0.35, mix=0.25, pp=0, hc=6000.0, sync=0, div=10):
    return {"delay_on": 1, "delay_time": time, "delay_feedback": fb, "delay_mix": mix, "delay_pingpong": pp,
            "delay_highcut": hc, "delay_sync": sync, "delay_div": div}
def reverb(size=0.6, damp=0.5, width=1.0, pre=20.0, mix=0.25):
    return {"reverb_on": 1, "reverb_size": size, "reverb_damping": damp, "reverb_width": width,
            "reverb_predelay": pre, "reverb_mix": mix}
def P(*parts):
    d = {}
    for x in parts: d.update(x)
    return d

PRESETS = [
  # ---------------------------------------------------------------- Bass
  ("Bass", "Sub Foundation", P(osc(1, 0, 1.0), osc(2, 3, 0.35, 12), filt(0, 900, 0.1, 0.2, 0.15),
                              fenv(0.001, 0.15, 0.0, 0.1), amp(0.002, 0.3, 0.9, 0.08))),
  ("Bass", "Acid Squelch",   P(osc(1, 1, 1.0), osc(2, 2, 0.45, 0, -6), filt(0, 220, 0.82, 0.3, 0.62),
                              fenv(0.001, 0.22, 0.05, 0.15), amp(0.001, 0.4, 0.7, 0.08),
                              lfo(2, 5, 1, 0.12, sync=1, div=12, mode=1),
                              delay(sync=1, div=10, fb=0.3, mix=0.18, pp=1, hc=3000))),
  ("Bass", "Wobble Bass",    P(osc(1, 1, 0.9), osc(2, 2, 0.7, -12), osc(3, 1, 0.5, 0, 8), filt(0, 300, 0.55, 0.2),
                              amp(0.003, 0.3, 1.0, 0.1), lfo(1, 0, 1, 0.55, sync=1, div=9, mode=0),
                              reverb(0.35, 0.6, 0.6, 5, 0.08))),
  ("Bass", "Deep Reese",     P(osc(1, 1, 0.8), osc(2, 1, 0.8, 0, 14), osc(3, 1, 0.6, -12), filt(0, 600, 0.2, 0.3),
                              amp(0.005, 0.3, 1.0, 0.15), lfo(1, 1, 5, 0.012, rate=0.3, mode=1),
                              chorus(0.3, 0.4, 0.25))),
  # ---------------------------------------------------------------- Lead
  ("Lead", "Forge Saw Lead", P(osc(1, 1, 0.8), osc(2, 1, 0.8, 0, 7), osc(3, 2, 0.4, -12), filt(0, 2800, 0.3, 0.3, 0.25),
                              fenv(0.002, 0.3, 0.4, 0.3), amp(0.005, 0.2, 0.85, 0.25),
                              lfo(1, 0, 3, 0.025, rate=5.5, delay=0.35),
                              delay(sync=1, div=10, fb=0.35, mix=0.22, pp=1), reverb(0.5, 0.5, 1.0, 20, 0.15))),
  ("Lead", "Hollow Square",  P(osc(1, 2, 0.9), osc(2, 2, 0.35, 12, 4), filt(0, 3500, 0.15, 0.2),
                              amp(0.01, 0.2, 0.9, 0.2), lfo(1, 1, 3, 0.03, rate=6.0), {"lfo1_modwheel": 1},
                              lfo(2, 0, 8, 0.4, rate=0.25, mode=1),
                              delay(time=320, fb=0.3, mix=0.2), reverb(0.55, 0.5, 1.0, 15, 0.2))),
  ("Lead", "Resonant Scream", P(osc(1, 1, 0.9), osc(2, 1, 0.6, 12, -5), filt(0, 1200, 0.7, 0.4, 0.35),
                               fenv(0.05, 0.6, 0.5, 0.4), amp(0.004, 0.3, 0.9, 0.3),
                               lfo(2, 1, 1, 0.25, rate=0.15, mode=1), chorus(0.5, 0.4, 0.3),
                               delay(sync=1, div=6, fb=0.4, mix=0.2, pp=1, hc=4000), reverb(0.6, 0.5, 1.0, 25, 0.18))),
  ("Lead", "Sine Whistle",   P(osc(1, 0, 1.0), osc(2, 3, 0.25, 12), amp(0.03, 0.2, 0.9, 0.35),
                              lfo(1, 0, 3, 0.015, rate=5.0, delay=0.25),
                              delay(sync=1, div=10, fb=0.4, mix=0.25, pp=1), reverb(0.7, 0.4, 1.0, 30, 0.3))),
  # ---------------------------------------------------------------- Pad
  ("Pad", "Warm Strings",    P(osc(1, 1, 0.7), osc(2, 1, 0.7, 0, 9), osc(3, 1, 0.7, 0, -8), filt(0, 2200, 0.1, 0.4),
                              amp(0.6, 1.0, 0.85, 1.2), lfo(1, 1, 1, 0.15, rate=0.2, mode=1),
                              chorus(0.5, 0.6, 0.4), reverb(0.8, 0.5, 1.0, 30, 0.35))),
  ("Pad", "Glass Pad",       P(osc(1, 3, 0.9), osc(2, 0, 0.5, 12), osc(3, 0, 0.25, 19), filt(0, 5000, 0.0, 0.2),
                              amp(0.8, 1.0, 0.9, 1.6), lfo(2, 0, 9, 0.6, rate=0.18, mode=1),
                              chorus(0.4, 0.5, 0.35), delay(sync=1, div=7, fb=0.4, mix=0.2, pp=1, hc=5000),
                              reverb(0.9, 0.35, 1.0, 40, 0.45))),
  ("Pad", "Dark Swell",      P(osc(1, 2, 0.8), osc(2, 1, 0.8, 0, 6), osc(3, 4, 0.08), filt(0, 400, 0.35, 0.2, 0.35),
                              fenv(2.0, 1.5, 0.6, 1.5), amp(1.5, 1.0, 0.9, 2.0), lfo(1, 0, 2, 0.2, rate=0.1, mode=1),
                              reverb(0.85, 0.6, 1.0, 50, 0.4))),
  ("Pad", "Pulse Motion",    P(osc(1, 2, 0.8), osc(2, 1, 0.5, -12), filt(0, 900, 0.45, 0.3),
                              amp(0.3, 0.5, 0.9, 0.8), lfo(1, 4, 1, 0.3, sync=1, div=9, mode=1),
                              lfo(2, 4, 10, 0.8, sync=1, div=12, mode=1), chorus(0.6, 0.5, 0.35),
                              delay(sync=1, div=10, fb=0.4, mix=0.25, pp=1, hc=5000), reverb(0.6, 0.5, 1.0, 20, 0.3))),
  # ---------------------------------------------------------------- Pluck
  ("Pluck", "Bright Pluck",  P(osc(1, 1, 0.9), osc(2, 2, 0.4, 12), filt(0, 400, 0.35, 0.4, 0.7),
                              fenv(0.001, 0.18, 0.0, 0.2), amp(0.001, 0.35, 0.0, 0.25),
                              delay(sync=1, div=10, fb=0.35, mix=0.25, pp=1), reverb(0.5, 0.5, 1.0, 15, 0.2))),
  ("Pluck", "Wood Mallet",   P(osc(1, 0, 1.0), osc(2, 0, 0.25, 24), osc(3, 3, 0.2, 12), filt(0, 3000, 0.0, 0.5, 0.3),
                              fenv(0.001, 0.08, 0.0, 0.1), amp(0.001, 0.5, 0.0, 0.3), reverb(0.4, 0.5, 0.8, 10, 0.25))),
  ("Pluck", "Nylon Harp",    P(osc(1, 3, 1.0), osc(2, 1, 0.3, 0, 5), filt(0, 1200, 0.1, 0.6, 0.5),
                              fenv(0.001, 0.4, 0.0, 0.4), amp(0.001, 0.9, 0.0, 0.6), chorus(0.4, 0.3, 0.2),
                              reverb(0.65, 0.5, 1.0, 20, 0.3))),
  ("Pluck", "Echo Drops",    P(osc(1, 2, 0.9), osc(2, 0, 0.5, 12), filt(0, 700, 0.45, 0.4, 0.5),
                              fenv(0.001, 0.12, 0.0, 0.1), amp(0.001, 0.2, 0.0, 0.15),
                              lfo(2, 5, 1, 0.3, sync=1, div=12, mode=1),
                              delay(sync=1, div=10, fb=0.55, mix=0.35, pp=1, hc=3500), reverb(0.55, 0.5, 1.0, 20, 0.25))),
  # ---------------------------------------------------------------- Keys
  ("Keys", "Electric Tines", P(osc(1, 0, 1.0), osc(2, 0, 0.15, 24), osc(3, 3, 0.3), filt(0, 4000, 0.0, 0.3, 0.2),
                              fenv(0.001, 0.3, 0.0, 0.3), amp(0.002, 1.6, 0.25, 0.4),
                              lfo(1, 0, 10, 0.25, rate=4.5, mode=1), chorus(0.7, 0.4, 0.3), reverb(0.5, 0.5, 1.0, 15, 0.2))),
  ("Keys", "Drawbar Organ",  P(osc(1, 0, 0.9), osc(2, 0, 0.6, 12), osc(3, 0, 0.4, 19), amp(0.005, 0.1, 1.0, 0.06),
                              chorus(4.5, 0.35, 0.5), reverb(0.45, 0.5, 1.0, 10, 0.15))),
  ("Keys", "Funk Clav",      P(osc(1, 2, 0.9), osc(2, 1, 0.4, 12, 3), filt(0, 900, 0.45, 0.6, 0.55),
                              fenv(0.001, 0.15, 0.0, 0.1), amp(0.001, 0.35, 0.25, 0.08), delay(time=90, fb=0.15, mix=0.12))),
  ("Keys", "Felt Keys",      P(osc(1, 3, 1.0), osc(2, 1, 0.25, 0, -4), filt(0, 1500, 0.05, 0.5, 0.35),
                              fenv(0.001, 0.6, 0.2, 0.4), amp(0.003, 2.0, 0.15, 0.5), reverb(0.6, 0.6, 1.0, 20, 0.3))),
  # ---------------------------------------------------------------- FX
  ("FX", "Rising Sweep",     P(osc(1, 4, 0.7), osc(2, 1, 0.8), osc(3, 1, 0.6, 0, 12), filt(0, 150, 0.55, 0.0, 0.8), fenv(2.5, 1.0, 1.0, 2.0),
                              amp(1.5, 1.0, 1.0, 2.0), lfo(1, 0, 4, 0.05, rate=0.5, mode=1),
                              delay(sync=1, div=7, fb=0.45, mix=0.25, pp=1, hc=5000), reverb(0.85, 0.4, 1.0, 40, 0.45))),
  ("FX", "Sci-Fi Bleeps",    P(osc(1, 2, 0.9), filt(0, 2500, 0.5, 0.3), amp(0.001, 0.2, 0.8, 0.1),
                              lfo(1, 5, 3, 0.5, sync=1, div=12, mode=0),
                              delay(sync=1, div=13, fb=0.5, mix=0.3, pp=1, hc=4500), reverb(0.5, 0.5, 1.0, 15, 0.25))),
  ("FX", "Ocean Wind",       P(osc(1, 4, 1.0), filt(2, 900, 0.3), amp(1.2, 1.0, 1.0, 2.5),
                              lfo(1, 0, 1, 0.6, rate=0.15, mode=1), lfo(2, 1, 10, 0.4, rate=0.11, mode=1),
                              chorus(0.3, 0.6, 0.4), reverb(0.9, 0.4, 1.0, 30, 0.4))),
  ("FX", "Laser Zap",        P(osc(1, 1, 0.9), osc(2, 2, 0.4, 7), filt(0, 3000, 0.4, 0.3), amp(0.001, 0.25, 0.0, 0.2),
                              lfo(1, 3, 3, 1.0, rate=6.0, mode=0),
                              delay(sync=1, div=9, fb=0.45, mix=0.3, pp=1, hc=5000), reverb(0.5, 0.5, 1.0, 15, 0.3))),
]

def main():
    levels_path = os.path.join(HERE, "levels.json")
    levels = json.load(open(levels_path)) if os.path.exists(levels_path) else {}
    names = [n for _, n, _ in PRESETS]
    assert len(names) == len(set(names)), "duplicate preset names"
    for cat, name, params in PRESETS:
        params = dict(params)
        params["master_gain"] = round(levels.get(name, -6.0), 1)
        d = os.path.join(OUT, cat)
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, name + ".vfpreset"), "w") as f:
            f.write('<?xml version="1.0" encoding="UTF-8"?>\n\n')
            f.write(f'<SynSyn1000Preset format="1" name={quoteattr(name)} category="{cat}" author="DoubleDouble Instrumentations" version="0.5.0">\n')
            f.write('  <SynSyn1000>\n')
            for k in sorted(params):
                f.write(f'    <PARAM id="{k}" value="{params[k]}"/>\n')
            f.write('  </SynSyn1000>\n</SynSyn1000Preset>\n')
    print(f"wrote {len(PRESETS)} presets to {OUT}")

if __name__ == "__main__":
    main()
