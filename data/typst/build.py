#!/usr/bin/env python3
"""Convert jgm/typst-symbols into our typst.tab format.

The fzf-typst IM mirrors the latex IM's transparent-until-backslash behavior,
so every visible lookup keyword is prefixed with a backslash. We also emit
hidden `@variant:*` keywords used by the addon to bias ranking toward a chosen
style without showing those control tags in the candidate comment.
"""

from __future__ import annotations

import argparse
import re
import sys
import unicodedata
from collections import OrderedDict, defaultdict
from dataclasses import dataclass
from pathlib import Path


STYLE_KEYWORDS = (
    "bold",
    "italic",
    "bolditalic",
    "cal",
    "boldcal",
    "bb",
    "frak",
    "boldfrak",
)
VARIANT_KEYWORD = {
    "bold": "@variant:bold",
    "italic": "@variant:italic",
    "bolditalic": "@variant:bolditalic",
    "cal": "@variant:cal",
    "boldcal": "@variant:boldcal",
    "bb": "@variant:bb",
    "frak": "@variant:frak",
    "boldfrak": "@variant:boldfrak",
    "upright": "@variant:upright",
}
STYLED_NAME_MARKERS = (
    "DOUBLE-STRUCK",
    "SCRIPT",
    "FRAKTUR",
    "BOLD",
    "ITALIC",
    "SANS-SERIF",
    "MONOSPACE",
)
VARIATION_SELECTORS = ("\ufe0e", "\ufe0f")
SPECIAL_STYLE_CODEPOINTS = {
    ("italic", "h"): 0x210E,
    ("cal", "B"): 0x212C,
    ("cal", "E"): 0x2130,
    ("cal", "F"): 0x2131,
    ("cal", "H"): 0x210B,
    ("cal", "I"): 0x2110,
    ("cal", "L"): 0x2112,
    ("cal", "M"): 0x2133,
    ("cal", "R"): 0x211B,
    ("cal", "e"): 0x212F,
    ("cal", "g"): 0x210A,
    ("cal", "o"): 0x2134,
    ("bb", "C"): 0x2102,
    ("bb", "H"): 0x210D,
    ("bb", "N"): 0x2115,
    ("bb", "P"): 0x2119,
    ("bb", "Q"): 0x211A,
    ("bb", "R"): 0x211D,
    ("bb", "Z"): 0x2124,
    ("frak", "C"): 0x212D,
    ("frak", "H"): 0x210C,
    ("frak", "I"): 0x2111,
    ("frak", "R"): 0x211C,
    ("frak", "Z"): 0x2128,
}


@dataclass(frozen=True)
class BaseSymbol:
    name: str
    value: str
    aliases: tuple[str, ...]


SYMBOL_RE = re.compile(
    r"Sym\s*"
    r"\{\s*symName = \"([^\"]+)\"\s*"
    r",\s*symIsAccent = (True|False)\s*"
    r",\s*symDeprecation = (?:Nothing|Just \"[^\"]*\")\s*"
    r",\s*symMathClass = ([A-Za-z]+)\s*"
    r",\s*symText = \"([^\"]*)\"\s*"
    r"\}",
    re.S,
)


def decode_haskell_string(text: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch != "\\":
            out.append(ch)
            i += 1
            continue
        i += 1
        if i >= len(text):
            out.append("\\")
            break
        esc = text[i]
        if esc.isdigit():
            j = i
            while j < len(text) and text[j].isdigit():
                j += 1
            out.append(chr(int(text[i:j], 10)))
            i = j
            continue
        mapping = {
            "\\": "\\",
            '"': '"',
            "n": "\n",
            "r": "\r",
            "t": "\t",
            "&": "",
        }
        out.append(mapping.get(esc, esc))
        i += 1
    return "".join(out)


def parse_symbols(text: str, emoji_values: set[str]) -> list[BaseSymbol]:
    shorthand_map = parse_shorthands(text)
    symbols: list[BaseSymbol] = []
    for name, _is_accent, _math_class, raw_value in SYMBOL_RE.findall(text):
        value = decode_haskell_string(raw_value)
        if is_emoji_value(value, emoji_values):
            continue
        aliases = [f"\\{name}"]
        for shorthand in shorthand_map.get(name, ()):
            aliases.append(f"\\{shorthand}")
        symbols.append(BaseSymbol(name=name, value=value,
                                  aliases=tuple(dedupe(aliases))))
    return symbols


def strip_variation_selectors(text: str) -> str:
    for selector in VARIATION_SELECTORS:
        text = text.replace(selector, "")
    return text


def load_emoji_values(path: Path | None) -> set[str]:
    if path is None:
        return set()

    values: set[str] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or ";" not in line:
            continue
        codepoints, _status = line.split(";", 1)
        value = "".join(chr(int(part, 16)) for part in codepoints.split())
        values.add(value)
        values.add(strip_variation_selectors(value))
    return values


def is_emoji_value(value: str, emoji_values: set[str]) -> bool:
    if not emoji_values:
        return False
    return value in emoji_values or strip_variation_selectors(value) in emoji_values


def parse_shorthands(text: str) -> dict[str, list[str]]:
    if "mathSymbolShorthands =" not in text:
        return {}
    _, shorthand_block = text.split("mathSymbolShorthands =", 1)
    mapping: dict[str, list[str]] = defaultdict(list)
    for shorthand, name in re.findall(r'\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\)',
                                      shorthand_block):
        mapping[name].append(shorthand)
    return mapping


def dedupe(items: list[str]) -> list[str]:
    seen: set[str] = set()
    out: list[str] = []
    for item in items:
        if item in seen:
            continue
        seen.add(item)
        out.append(item)
    return out


def synthetic_base_symbols() -> list[BaseSymbol]:
    symbols: list[BaseSymbol] = []
    for codepoint in range(ord("A"), ord("Z") + 1):
        ch = chr(codepoint)
        symbols.append(BaseSymbol(name=ch, value=ch, aliases=(f"\\{ch}",)))
    for codepoint in range(ord("a"), ord("z") + 1):
        ch = chr(codepoint)
        symbols.append(BaseSymbol(name=ch, value=ch, aliases=(f"\\{ch}",)))
    for digit in "0123456789":
        symbols.append(BaseSymbol(name=digit, value=digit, aliases=(f"\\{digit}",)))
    return symbols


def sort_symbols(symbols: list[BaseSymbol]) -> list[BaseSymbol]:
    return sorted(
        symbols,
        key=lambda symbol: (
            symbol.name.casefold(),
            0 if symbol.name == symbol.name.casefold() else 1,
            symbol.name,
        ),
    )


def styled_value(base_value: str, style: str) -> str | None:
    if style == "upright":
        return base_value
    if len(base_value) != 1:
        return None
    if codepoint := SPECIAL_STYLE_CODEPOINTS.get((style, base_value)):
        return chr(codepoint)
    name = unicodedata.name(base_value, "")
    if not name or any(marker in name for marker in STYLED_NAME_MARKERS):
        return None

    target_name = None
    if name.startswith("LATIN CAPITAL LETTER "):
        target_name = style_name(style, "CAPITAL", name.removeprefix("LATIN CAPITAL LETTER "))
    elif name.startswith("LATIN SMALL LETTER "):
        target_name = style_name(style, "SMALL", name.removeprefix("LATIN SMALL LETTER "))
    elif name.startswith("GREEK CAPITAL LETTER "):
        target_name = style_name(style, "CAPITAL", name.removeprefix("GREEK CAPITAL LETTER "))
    elif name.startswith("GREEK SMALL LETTER "):
        target_name = style_name(style, "SMALL", name.removeprefix("GREEK SMALL LETTER "))
    elif name.startswith("DIGIT "):
        target_name = digit_style_name(style, name.removeprefix("DIGIT "))

    if not target_name:
        return None
    try:
        return unicodedata.lookup(target_name)
    except KeyError:
        return None


def style_name(style: str, size: str, payload: str) -> str | None:
    style_prefix = {
        "bold": "MATHEMATICAL BOLD",
        "italic": "MATHEMATICAL ITALIC",
        "bolditalic": "MATHEMATICAL BOLD ITALIC",
        "cal": "MATHEMATICAL SCRIPT",
        "boldcal": "MATHEMATICAL BOLD SCRIPT",
        "bb": "MATHEMATICAL DOUBLE-STRUCK",
        "frak": "MATHEMATICAL FRAKTUR",
        "boldfrak": "MATHEMATICAL BOLD FRAKTUR",
    }.get(style)
    if not style_prefix:
        return None
    return f"{style_prefix} {size} {payload}"


def digit_style_name(style: str, payload: str) -> str | None:
    style_prefix = {
        "bold": "MATHEMATICAL BOLD",
        "bb": "MATHEMATICAL DOUBLE-STRUCK",
    }.get(style)
    if not style_prefix:
        return None
    return f"{style_prefix} DIGIT {payload}"


def add_row(rows: OrderedDict[tuple[str, str], list[str]],
            value: str,
            visible_aliases: tuple[str, ...],
            hidden_variant: str) -> None:
    key = (value, hidden_variant)
    if key not in rows:
        rows[key] = []
    for alias in visible_aliases:
        if alias not in rows[key]:
            rows[key].append(alias)
    if hidden_variant not in rows[key]:
        rows[key].append(hidden_variant)


def build_rows(symbols: list[BaseSymbol]) -> OrderedDict[tuple[str, str], list[str]]:
    rows: OrderedDict[tuple[str, str], list[str]] = OrderedDict()

    for symbol in symbols:
        add_row(rows, symbol.value, symbol.aliases, VARIANT_KEYWORD["upright"])
        for style in STYLE_KEYWORDS:
            value = styled_value(symbol.value, style)
            if not value or value == symbol.value:
                continue
            add_row(rows, value, symbol.aliases, VARIANT_KEYWORD[style])

    return rows


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", required=True, type=Path,
                    help="path to typst-symbols src/Typst/Symbols.hs")
    ap.add_argument("--emoji-test", type=Path,
                    help="Unicode emoji-test.txt used to exclude emoji rows")
    ap.add_argument("--out", required=True, type=Path,
                    help="output typst.tab path")
    args = ap.parse_args()

    text = args.source.read_text(encoding="utf-8")
    emoji_values = load_emoji_values(args.emoji_test)
    symbols = synthetic_base_symbols() + sort_symbols(
        parse_symbols(text, emoji_values))
    rows = build_rows(symbols)

    with args.out.open("w", encoding="utf-8") as f:
        f.write("# typst.tab generated from jgm/typst-symbols.\n")
        f.write("# Emoji rows from typst-symbols are filtered out.\n")
        f.write("# Hidden @variant:* keywords are used only for IM-side ranking.\n")
        for (value, _variant), keywords in rows.items():
            cols = [value.replace("\t", " ").replace("\n", " ")]
            cols.extend(keyword.replace("\t", " ").replace("\n", " ")
                        for keyword in keywords)
            f.write("\t".join(cols) + "\n")

    print(f"wrote {len(rows)} rows to {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
