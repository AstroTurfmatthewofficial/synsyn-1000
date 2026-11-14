#!/usr/bin/env python3
"""Regenerate THIRD_PARTY_NOTICES.txt from the licence files shipped inside external/JUCE
(and, for the Windows build, the MinGW-w64 runtime copyright file). Run from the repo root."""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
J = os.path.join(ROOT, "external/JUCE")
M = "modules"
HP = M + "/juce_audio_processors_headless/format_types"

COMPONENTS = [
    # (name, licence, path inside JUCE, section-extractor or None, note)
    ("JUCE 8.0.15 framework", "AGPL-3.0 (used under the AGPLv3 option of JUCE's dual licence)", "LICENSE.md", None,
     "The full AGPLv3 text is in LICENSE. JUCE source: https://github.com/juce-framework/JUCE (tag 8.0.15), used unmodified."),
    ("Steinberg VST 3 SDK (base)", "MIT", HP + "/VST3_SDK/base/LICENSE.txt", None,
     "VST is a registered trademark of Steinberg Media Technologies GmbH."),
    ("Steinberg VST 3 SDK (pluginterfaces)", "MIT", HP + "/VST3_SDK/pluginterfaces/LICENSE.txt", None, ""),
    ("Steinberg VST 3 SDK (public.sdk)", "MIT", HP + "/VST3_SDK/public.sdk/LICENSE.txt", None, ""),
    ("LV2", "ISC", HP + "/LV2_SDK/lv2/COPYING", None, ""),
    ("serd", "ISC", HP + "/LV2_SDK/serd/COPYING", None, ""),
    ("sord", "ISC", HP + "/LV2_SDK/sord/COPYING", None, ""),
    ("sratom", "ISC", HP + "/LV2_SDK/sratom/COPYING", None, ""),
    ("lilv", "ISC", HP + "/LV2_SDK/lilv/COPYING", None, ""),
    ("zlib", "zlib", M + "/juce_core/zip/zlib/LICENSE", None, ""),
    ("libpng", "libpng/zlib", M + "/juce_graphics/image_formats/pnglib/LICENSE", None, ""),
    ("Independent JPEG Group jpeglib", "IJG", M + "/juce_graphics/image_formats/jpglib/README", "LEGAL ISSUES",
     "This software is based in part on the work of the Independent JPEG Group."),
    ("HarfBuzz", "Old MIT", M + "/juce_graphics/fonts/harfbuzz/COPYING", None, ""),
    ("SheenBidi", "Apache-2.0", M + "/juce_graphics/unicode/sheenbidi/LICENSE", None, ""),
    ("FLAC", "BSD-3-Clause", M + "/juce_audio_formats/codecs/flac/Flac Licence.txt", None, ""),
    ("Ogg Vorbis", "BSD-3-Clause", M + "/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt", None, ""),
]

def section(text, title):
    m = re.search(r"^" + re.escape(title) + r"\s*\n=+\n(.*?)(?=\n[A-Z][A-Z /]+\n=+\n|\Z)", text, re.S | re.M)
    return (title + "\n" + m.group(1)).strip() if m else text

def main(windows=False):
    out = ["SynSyn 1000 - third-party notices",
           "=================================", "",
           "SynSyn 1000 is Copyright (C) 2026 DoubleDouble Instrumentations and is licensed under the",
           "GNU Affero General Public License v3.0 or later (see LICENSE). It is built with the components",
           "below. Their licence texts follow, reproduced from the copies shipped with JUCE 8.0.15.",
           "Some components (e.g. FLAC/Ogg Vorbis/jpeglib) are compiled in by JUCE even if unused by SynSyn 1000.", ""]
    for name, lic, rel, sec, note in COMPONENTS:
        out.append(f"- {name}: {lic}")
    out.append("- MinGW-w64 runtime / winpthreads (Windows build only): see the Windows section at the end")
    out.append("- GCC runtime libraries libgcc / libstdc++ (statically linked in the Windows build): GPL-3.0 with the GCC Runtime Library Exception 3.1")
    out.append("")
    for name, lic, rel, sec, note in COMPONENTS:
        out += ["", "=" * 78, f"{name} ({lic})", f"Source file: external/JUCE/{rel}", "=" * 78]
        if note: out.append(note)
        if rel == "LICENSE.md":
            text = open(os.path.join(J, rel), encoding="utf-8", errors="replace").read()
            out.append(text.strip())
            continue
        text = open(os.path.join(J, rel), encoding="utf-8", errors="replace").read()
        out.append(section(text, sec) if sec else text.strip())
    if windows:
        cp = "/usr/share/doc/mingw-w64-x86-64-dev/copyright"
        out += ["", "=" * 78, "Windows build: MinGW-w64 headers, CRT start-up code and winpthreads",
                f"Source file: {cp} (Debian package mingw-w64-x86-64-dev 12.0.0-5)", "=" * 78,
                open(cp, encoding="utf-8", errors="replace").read().strip(),
                "", "=" * 78, "Windows build: GCC runtime (libgcc, libstdc++) - GCC 14.2 (Debian gcc-mingw-w64 14.2.0-19)", "=" * 78,
                "libgcc and libstdc++ are licensed under the GNU General Public License v3 with the GCC Runtime",
                "Library Exception, version 3.1, which permits distributing the statically linked result under",
                "terms of your choice. See https://www.gnu.org/licenses/gcc-exception-3.1.html"]
    return "\n".join(out) + "\n"

if __name__ == "__main__":
    windows = "--windows" in sys.argv
    dest = sys.argv[-1] if len(sys.argv) > 1 and not sys.argv[-1].startswith("--") else os.path.join(ROOT, "THIRD_PARTY_NOTICES.txt")
    open(dest, "w", encoding="utf-8").write(main(windows))
    print("wrote", dest)
