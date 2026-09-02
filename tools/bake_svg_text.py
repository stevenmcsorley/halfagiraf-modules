#!/usr/bin/env python3
"""Convert the editable panel SVG's text nodes to paths for NanoSVG/Rack."""

from __future__ import annotations

import argparse
import html
import re
from pathlib import Path
from xml.etree import ElementTree

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont
from matplotlib import font_manager


CSS_RULE = re.compile(r"\.([\w-]+)\s*\{([^}]*)\}", re.DOTALL)
TEXT_NODE = re.compile(r"<text\b([^>]*)>(.*?)</text>", re.DOTALL)


def declarations(source: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for item in source.split(";"):
        if ":" in item:
            key, value = item.split(":", 1)
            result[key.strip()] = value.strip()
    return result


def number(value: str, default: float = 0.0) -> float:
    match = re.search(r"[-+]?(?:\d*\.\d+|\d+)", value or "")
    return float(match.group()) if match else default


class Face:
    def __init__(self, bold: bool) -> None:
        properties = font_manager.FontProperties(family="DejaVu Sans", weight="bold" if bold else "normal")
        self.font = TTFont(font_manager.findfont(properties))
        self.units = self.font["head"].unitsPerEm
        self.cmap = self.font.getBestCmap()
        self.glyphs = self.font.getGlyphSet()
        self.metrics = self.font["hmtx"]

    def path(self, text: str, x: float, y: float, size: float, spacing: float, anchor: str) -> str:
        advances = [self.metrics[self.cmap.get(ord(char), ".notdef")][0] for char in text]
        scale = size / self.units
        width = sum(advances) * scale + spacing * max(0, len(text) - 1)
        start = x - width / 2 if anchor == "middle" else x - width if anchor == "end" else x
        pen = SVGPathPen(self.glyphs)
        cursor = start
        for char, advance in zip(text, advances):
            glyph_name = self.cmap.get(ord(char))
            if glyph_name:
                transformed = TransformPen(pen, (scale, 0, 0, -scale, cursor, y))
                self.glyphs[glyph_name].draw(transformed)
            cursor += advance * scale + spacing
        return pen.getCommands()


def bake(source: str) -> str:
    styles = {name: declarations(body) for name, body in CSS_RULE.findall(source)}
    faces = {False: Face(False), True: Face(True)}

    def replace(match: re.Match[str]) -> str:
        attrs = ElementTree.fromstring(f"<text {match.group(1)} />").attrib
        style: dict[str, str] = {}
        for class_name in attrs.get("class", "").split():
            style.update(styles.get(class_name, {}))
        style.update(declarations(attrs.get("style", "")))
        for key in ("fill", "font-size", "font-weight", "letter-spacing", "text-anchor", "opacity", "transform"):
            if key in attrs:
                style[key] = attrs[key]

        text = html.unescape(re.sub(r"<[^>]+>", "", match.group(2)))
        bold = number(style.get("font-weight", "400"), 400) >= 600
        path = faces[bold].path(
            text,
            number(attrs.get("x", "0")),
            number(attrs.get("y", "0")),
            number(style.get("font-size", "2"), 2),
            number(style.get("letter-spacing", "0")),
            style.get("text-anchor", "start"),
        )
        extra = ""
        for key in ("opacity", "transform"):
            if key in style:
                extra += f' {key}="{style[key]}"'
        return f'<path d="{path}" fill="{style.get("fill", "#000000")}"{extra} />'

    baked = TEXT_NODE.sub(replace, source)
    return "\n".join(line.rstrip() for line in baked.splitlines()) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    # Write bytes so Windows does not translate the generated LF endings to CRLF.
    args.destination.write_bytes(bake(args.source.read_text(encoding="utf-8")).encode("utf-8"))


if __name__ == "__main__":
    main()
