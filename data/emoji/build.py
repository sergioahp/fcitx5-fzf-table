#!/usr/bin/env python3
"""Generate emoji.tab from CLDR annotations + annotationsDerived.

Each emoji has two sibling <annotation> rows in CLDR:

    <annotation cp="X">kw1 | kw2 | kw3</annotation>
    <annotation cp="X" type="tts">short name</annotation>

We pair them by `cp` and write one TSV line per emoji:

    <emoji>\\t<short>\\t<kw1>\\t<kw2>...

CLDR also annotates non-emoji characters (currencies, math symbols,
keyboard punctuation) for screen-reader purposes. Unicode's
emoji-test.txt is the authoritative list of what is an emoji, so we
use it as a membership filter: any CLDR row whose `cp` (after stripping
U+FE0F to match CLDR's keying convention) is not in emoji-test.txt is
dropped.

Order is preserved from the input files (annotations.xml first, then
annotationsDerived.xml), so search ties break in CLDR's authoring order
(smileys before flags before symbols) rather than codepoint order.
"""

from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def load_xml(path: Path) -> dict[str, dict]:
    """Walk all <annotation> elements; return {cp: {short, keywords}}.

    Insertion order matches the file order, which is what we want for
    tie-breaking in fuzzy ranking.
    """
    tree = ET.parse(path)
    annotations = tree.getroot().find("annotations")
    if annotations is None:
        raise RuntimeError(f"{path}: no <annotations> element")

    out: dict[str, dict] = {}
    for ann in annotations.findall("annotation"):
        cp = ann.get("cp")
        if not cp:
            continue
        entry = out.setdefault(cp, {"short": None, "keywords": []})
        text = (ann.text or "").strip()
        if ann.get("type") == "tts":
            if text:
                entry["short"] = text
        else:
            kws = [k.strip() for k in text.split("|")]
            entry["keywords"] = [k for k in kws if k]
    return out


def load_emoji_test(path: Path) -> set[str]:
    """Build set of FE0F-stripped emoji sequences from emoji-test.txt.

    Lines look like:
        1F636 200D 1F32B FE0F  ; fully-qualified  # 😶‍🌫️ E13.1 face in clouds

    We collect rows of any qualification status (component / fully-qualified
    / minimally-qualified / unqualified). Stripping FE0F lets us compare
    directly against CLDR's `cp` attribute, which has FE0F removed.
    """
    out: set[str] = set()
    with path.open(encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            head, sep, _ = stripped.partition(";")
            if not sep:
                continue
            try:
                chars = "".join(chr(int(c, 16)) for c in head.split())
            except ValueError:
                continue
            chars = chars.replace("\ufe0f", "")
            if chars:
                out.add(chars)
    return out


# Standalone Fitzpatrick skin-tone modifiers (U+1F3FB..U+1F3FF). These are
# combining characters meant to follow a base emoji; on their own they only
# render as a color swatch and are useless as table entries — the pre-composed
# rows (👋🏽 etc.) cover the actual user-picking case.
SKIN_TONE_MODIFIERS = frozenset(chr(c) for c in range(0x1F3FB, 0x1F3FF + 1))


def is_emoji_cp(cp: str, allowed: set[str]) -> bool:
    return cp in allowed


def merge(into: dict[str, dict], src: dict[str, dict]) -> None:
    for cp, e in src.items():
        if cp not in into:
            into[cp] = {"short": e["short"], "keywords": list(e["keywords"])}
            continue
        if not into[cp]["short"] and e["short"]:
            into[cp]["short"] = e["short"]
        seen = set(into[cp]["keywords"])
        for kw in e["keywords"]:
            if kw not in seen:
                into[cp]["keywords"].append(kw)
                seen.add(kw)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--annotations", required=True, type=Path,
                    help="path to common/annotations/en.xml")
    ap.add_argument("--derived", required=True, type=Path,
                    help="path to common/annotationsDerived/en.xml")
    ap.add_argument("--emoji-test", required=True, type=Path,
                    help="path to Unicode emoji-test.txt (membership filter)")
    ap.add_argument("--out", required=True, type=Path,
                    help="output emoji.tab path")
    args = ap.parse_args()

    allowed = load_emoji_test(args.emoji_test)

    merged: dict[str, dict] = {}
    merge(merged, load_xml(args.annotations))
    merge(merged, load_xml(args.derived))

    rows = 0
    skipped = 0
    skipped_tone = 0
    with args.out.open("w", encoding="utf-8") as f:
        f.write("# emoji.tab generated from CLDR annotations + annotationsDerived,\n")
        f.write("# filtered to RGI emoji via Unicode emoji-test.txt.\n")
        f.write("# Format: <emoji>\\t<short>\\t<kw1>\\t<kw2>...\n")
        for cp, entry in merged.items():
            if not is_emoji_cp(cp, allowed):
                skipped += 1
                continue
            if cp in SKIN_TONE_MODIFIERS:
                skipped_tone += 1
                continue
            short = entry["short"] or (entry["keywords"][0] if entry["keywords"] else "")
            if not short and not entry["keywords"]:
                continue
            cols = [cp, short, *entry["keywords"]]
            cols = [c.replace("\t", " ").replace("\n", " ") for c in cols]
            f.write("\t".join(cols) + "\n")
            rows += 1

    print(
        f"wrote {rows} rows "
        f"({skipped} non-emoji, {skipped_tone} bare skin-tone modifier rows skipped) "
        f"to {args.out}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
