#!/usr/bin/env python3
"""Convert upstream fcitx5-table-other latex.txt into our latex.tab format.

Upstream format (after the [Data] header line):

    \alpha α
    \Delta Δ
    \frac12 ½

Each line is `<code> <value>` separated by a single space; the value is
typically one codepoint but can be longer. Our table format is TSV with
the value first, followed by keywords; we keep upstream's code (including
the leading backslash) as the sole keyword, since the fzf-latex IM treats
the backslash as both the trigger character and the first character of
the query.

Upstream has a few "code=value=same" pairs like:

    \ \

which map an escaped space to a space. These would be useless in a fuzzy
picker (you'd never browse to them) so we skip them.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def parse_lines(text: str) -> list[tuple[str, str]]:
    in_data = False
    out: list[tuple[str, str]] = []
    for raw in text.splitlines():
        line = raw.rstrip("\n").rstrip("\r")
        if not in_data:
            if line.strip() == "[Data]":
                in_data = True
            continue
        if not line.strip():
            continue
        # Upstream format is `<code> <value>` with one space. value may
        # legitimately contain spaces only in extremely rare cases; the
        # one-space split is what upstream's own table loader uses.
        try:
            code, value = line.split(" ", 1)
        except ValueError:
            continue
        if not code or not value:
            continue
        # Skip trivial identity mappings - useless in a fuzzy picker.
        if code == value:
            continue
        out.append((code, value))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True, type=Path,
                    help="path to upstream latex.txt")
    ap.add_argument("--out", required=True, type=Path,
                    help="output latex.tab path")
    args = ap.parse_args()

    entries = parse_lines(args.source.read_text(encoding="utf-8"))

    # Stable dedup by (value, code). Upstream sometimes lists the same
    # code with the same value in two places; we keep the first.
    seen: set[tuple[str, str]] = set()
    rows: list[tuple[str, str]] = []
    for code, value in entries:
        key = (value, code)
        if key in seen:
            continue
        seen.add(key)
        rows.append((value, code))

    with args.out.open("w", encoding="utf-8") as f:
        f.write("# latex.tab generated from fcitx/fcitx5-table-other latex.txt.\n")
        f.write("# Format: <value>\\t<code>   (code keeps the leading backslash)\n")
        for value, code in rows:
            cols = [value.replace("\t", " ").replace("\n", " "),
                    code.replace("\t", " ").replace("\n", " ")]
            f.write("\t".join(cols) + "\n")

    print(f"wrote {len(rows)} rows to {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
