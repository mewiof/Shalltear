#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import re
import sys
import xml.etree.ElementTree as ElementTree
from pathlib import Path

try:
	from fontTools.pens.recordingPen import RecordingPen
	from fontTools.svgLib.path import parse_path
except ImportError:  # pragma: no cover - a developer-side tool, not a build step
	sys.exit("fontTools is required: python -m pip install fonttools")

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "Source/Editor/Public/Shalltear/Editor/EditorIcons.h"
OUTPUT = ROOT / "Source/Editor/Private/EditorIconGlyphs.generated.cpp"
DEFAULT_SOURCES = ROOT / "Scripts/IconBake/Icons"

FACES = ["Solid", "Regular"]

EM = 512

PAD_X = 20
PAD_Y = 44

ENTRY = re.compile(r'inline constexpr Icon (\w+)\{"([^"]*)"(?:\s*,\s*Face::(\w+))?\}')

# must match VectorVerb's values
VERB_MOVE, VERB_LINE, VERB_CUBIC, VERB_CLOSE = 0, 1, 2, 3

SVG = "{http://www.w3.org/2000/svg}"

FREE_NOTICE = re.compile(r"Font Awesome Free (\d+(?:\.\d+)+) by @fontawesome\b.*?Icons: CC BY 4\.0", re.DOTALL)
ORIGINAL_NOTICE = "Original artwork drawn for Shalltear"
REFUSED_NOTICE = re.compile(r"Font Awesome Pro|Commercial License", re.IGNORECASE)


def scrape_table(header: Path):
	text = header.read_text(encoding="utf-8")
	out = []
	for match in ENTRY.finditer(text):
		name, literal, face = match.group(1), match.group(2), match.group(3) or "Solid"
		if not literal:
			continue
		if literal.startswith("\\u"):
			codepoint = int(literal[2:6], 16)
		else:
			if len(literal) != 1:
				raise SystemExit(f"{name}: can't read the glyph literal {literal!r}")
			codepoint = ord(literal)
		if face not in FACES:
			raise SystemExit(f"{name}: unknown face {face!r}")
		out.append((name, codepoint, face))
	if not out:
		raise SystemExit(f"no Icon constants found in {header}. Scrape is likely broken")
	return out


def provenance(path: Path, text: str) -> str:
	notes = " ".join(re.findall(r"<!--(.*?)-->", text, re.DOTALL))
	if REFUSED_NOTICE.search(notes):
		raise SystemExit(f"{path.name}: a Font Awesome Pro icon can't be a bake source (license).\n"
						 f"Use the Font Awesome Free icon, or draw an original.")
	free = FREE_NOTICE.search(notes)
	if free:
		return f"Font Awesome Free {free.group(1)}"
	if ORIGINAL_NOTICE in notes:
		return "original"
	raise SystemExit(f"{path.name}: declares no license. A source is either a Font Awesome Free SVG copied\n"
					 f"unmodified (its comment has the license) or original artwork carrying the\n"
					 f"comment '{ORIGINAL_NOTICE}' (Scripts/IconBake/README.md).")


def read_source(path: Path):
	text = path.read_bytes().decode("utf-8")
	origin = provenance(path, text)
	try:
		root = ElementTree.fromstring(text)
	except ElementTree.ParseError as error:
		raise SystemExit(f"{path.name}: not well-formed XML ({error})")
	if root.tag != SVG + "svg":
		raise SystemExit(f"{path.name}: the root element is <{root.tag}>, not an SVG <svg>")
	for attribute in root.attrib:
		if attribute != "viewBox":
			raise SystemExit(f"{path.name}: <svg> carries `{attribute}`, which the bake doesn't read")
	box = [float(v) for v in root.attrib.get("viewBox", "").replace(",", " ").split()]
	if len(box) != 4 or box[0] != 0.0 or box[1] != 0.0 or box[3] != EM or box[2] <= 0.0:
		raise SystemExit(f"{path.name}: the viewBox must be '0 0 <advance> {EM}'. em the editor sizes "
						 f"icons in. {root.attrib.get('viewBox')!r}")

	pen = RecordingPen()
	paths = 0
	for element in root.iter():
		if element is root:
			continue
		if element.tag != SVG + "path":
			raise SystemExit(f"{path.name}: <{element.tag.replace(SVG, '')}> is outside the subset the bake reads")
		for attribute, value in element.attrib.items():
			if attribute == "d" or (attribute == "fill" and value == "currentColor"):
				continue
			raise SystemExit(f"{path.name}: <path {attribute}=\"{value}\"> is outside the subset the bake reads")
		parse_path(element.attrib.get("d", ""), pen)
		paths += 1
	if paths == 0:
		raise SystemExit(f"{path.name}: no <path> to bake")
	return contours_of(path, pen.value), box[2], origin


def contours_of(path: Path, recording):
	out, current, position = [], None, None
	for verb, args in recording:
		if verb == "moveTo":
			if current:
				out.append(current)
			position = args[0]
			current = [("move", [position])]
		elif verb == "lineTo":
			current.append(("line", [args[0]]))
			position = args[0]
		elif verb == "curveTo":
			points = list(args)
			if len(points) != 3:
				raise SystemExit(f"{path.name}: a {len(points)}-point curve isn't a cubic")
			current.append(("cubic", points))
			position = points[-1]
		elif verb == "qCurveTo":
			points = list(args)
			for index in range(len(points) - 1):
				control, end = points[index], points[index + 1]
				c1 = (position[0] + 2 / 3 * (control[0] - position[0]),
					  position[1] + 2 / 3 * (control[1] - position[1]))
				c2 = (end[0] + 2 / 3 * (control[0] - end[0]),
					  end[1] + 2 / 3 * (control[1] - end[1]))
				current.append(("cubic", [c1, c2, end]))
				position = end
		elif verb in ("closePath", "endPath"):
			if current:
				out.append(current)
			current = None
		else:
			raise SystemExit(f"{path.name}: unhandled pen verb {verb!r}")
	if current:
		out.append(current)
	return out


def _cubic_extrema(a, b, c, d):
	values = [a, d]
	qa = -a + 3.0 * b - 3.0 * c + d
	qb = 2.0 * (a - 2.0 * b + c)
	qc = -a + b
	roots = []
	if abs(qa) < 1e-12:
		if abs(qb) > 1e-12:
			roots.append(-qc / qb)
	else:
		disc = qb * qb - 4.0 * qa * qc
		if disc >= 0.0:
			root = disc ** 0.5
			roots.append((-qb + root) / (2.0 * qa))
			roots.append((-qb - root) / (2.0 * qa))
	for t in roots:
		if 0.0 < t < 1.0:
			u = 1.0 - t
			values.append(u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d)
	return values


def ink_bounds(contours):
	xs, ys = [], []
	position = None
	for contour in contours:
		for verb, points in contour:
			if verb in ("move", "line"):
				xs.append(points[0][0])
				ys.append(points[0][1])
				position = points[0]
			else:
				p0, (p1, p2, p3) = position, points
				xs.extend(_cubic_extrema(p0[0], p1[0], p2[0], p3[0]))
				ys.extend(_cubic_extrema(p0[1], p1[1], p2[1], p3[1]))
				position = p3
	if not xs:
		return None
	return min(xs), min(ys), max(xs), max(ys)


class Baked:
	__slots__ = ("name", "codepoint", "face", "origin", "verbs", "points", "view_w", "view_h", "size_scale", "clipped")


def bake(name, codepoint, face_name, source: Path) -> Baked:
	contours, advance, origin = read_source(source)

	box_x0 = 0 - PAD_X
	box_x1 = advance + PAD_X
	box_y0 = 0 - PAD_Y
	box_y1 = EM + PAD_Y

	ink = ink_bounds(contours)
	clipped = 0.0
	if ink is not None:
		clipped = max(box_x0 - ink[0], box_y0 - ink[1], ink[2] - box_x1, ink[3] - box_y1, 0.0)

	out = Baked()
	out.name = name
	out.codepoint = codepoint
	out.face = face_name
	out.origin = origin
	out.clipped = clipped
	out.view_w = float(box_x1 - box_x0)
	out.view_h = float(box_y1 - box_y0)
	out.size_scale = max(out.view_w, out.view_h) / float(EM)

	verbs, points = [], []

	def emit(verb, pts):
		verbs.append(verb)
		for x, y in pts:
			points.append((x - box_x0, y - box_y0))

	for contour in contours:
		for verb, pts in contour:
			if verb == "move":
				emit(VERB_MOVE, pts)
			elif verb == "line":
				emit(VERB_LINE, pts)
			else:
				emit(VERB_CUBIC, pts)
		verbs.append(VERB_CLOSE)

	out.verbs = verbs
	out.points = points
	return out


def fmt(value: float) -> str:
	if value == int(value) and abs(value) < 1e9:
		return f"{int(value)}.0f"
	text = f"{value:.6g}"
	if "." not in text and "e" not in text:
		text += ".0"
	return f"{text}f"


def emit(baked_all, sources_digest, header_digest) -> str:
	lines = []
	w = lines.append

	free = sorted({b.origin for b in baked_all if b.origin != "original"})
	originals = sum(1 for b in baked_all if b.origin == "original")

	w("// generated by Scripts/bake_icon_glyphs.py. Do not edit manually")
	for origin in free:
		count = sum(1 for b in baked_all if b.origin == origin)
		w(f"// {count} from {origin} by Fonticons, Inc. (https://fontawesome.com)")
		w("// licensed CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/)")
		w("// converted from the SVG paths into padded boxes with outlines unchanged")
	w(f"// {originals} original artwork drawn for Shalltear under the repository's LICENSE.txt")
	w(f"// sources sha256:{sources_digest}")
	w(f"// icon table sha256:{header_digest}")
	w("")
	w('#include "EditorIconGlyphs.generated.h"')
	w("")
	w("namespace Shalltear::EditorIcons::Baked {")
	w("")

	verbs = []
	points = []
	records = []
	for item in baked_all:
		records.append((item, len(verbs), len(item.verbs), len(points), len(item.points)))
		verbs.extend(item.verbs)
		points.extend(item.points)

	w("\t// VectorVerb values 0 Move 1 Line 2 Cubic 3 Close")
	w(f"\tconst u8 VERBS[{len(verbs)}] = {{")
	for start in range(0, len(verbs), 32):
		chunk = ", ".join(str(v) for v in verbs[start:start + 32])
		w(f"\t\t{chunk},")
	w("\t};")
	w("")

	w("\t// x y pairs in view box coordinates")
	w(f"\tconst f32 POINTS[{len(points) * 2}] = {{")
	for start in range(0, len(points), 6):
		chunk = ", ".join(f"{fmt(x)}, {fmt(y)}" for x, y in points[start:start + 6])
		w(f"\t\t{chunk},")
	w("\t};")
	w("")

	w("\t// sorted by face, then codepoint (Find binary searches)")
	w(f"\tconst Glyph GLYPHS[{len(records)}] = {{")
	for item, first_verb, verb_count, first_point, point_count in records:
		face_index = FACES.index(item.face)
		w(f"\t\t{{0x{item.codepoint:04X}, {face_index}, {first_verb}, {verb_count}, {first_point}, {point_count}, "
		  f"{fmt(item.view_w)}, {fmt(item.view_h)}, {item.size_scale:.8g}f}}, // {item.origin} {item.name}")
	w("\t};")
	w("")
	w(f"\tconst u32 GLYPH_COUNT = {len(records)};")
	w("")
	w("} // namespace Shalltear::EditorIcons::Baked")
	w("")
	return "\n".join(lines)


def emit_header() -> str:
	return "\n".join([
		"// generated by Scripts/bake_icon_glyphs.py. Do not edit manually",
		"#pragma once",
		"",
		'#include "Shalltear/Core/Types.h"',
		"",
		"namespace Shalltear::EditorIcons::Baked {",
		"",
		"\tstruct Glyph {",
		"\t\tu32 codepoint;",
		"\t\tu32 face; // idx into EditorIcons::Face",
		"\t\tu32 firstVerb;",
		"\t\tu32 verbCount;",
		"\t\tu32 firstPoint;",
		"\t\tu32 pointCount;",
		"\t\tf32 viewW;",
		"\t\tf32 viewH;",
		"\t\tf32 sizeScale;",
		"\t};",
		"",
		"\textern const u8 VERBS[];",
		"\textern const f32 POINTS[];",
		"\textern const Glyph GLYPHS[];",
		"\textern const u32 GLYPH_COUNT;",
		"",
		"} // namespace Shalltear::EditorIcons::Baked",
		"",
	])


def write_bytes(path: Path, text: str):
	data = text.replace("\r\n", "\n").encode("utf-8")
	data.decode("utf-8")
	path.write_bytes(data)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--sources", type=Path, default=DEFAULT_SOURCES,
						help="directory holding one <Name>.svg per table entry")
	parser.add_argument("--check", action="store_true",
						help="fail if the committed output differs (don't rewrite it)")
	args = parser.parse_args()

	table = scrape_table(HEADER)
	header_digest = hashlib.sha256(HEADER.read_bytes()).hexdigest()[:16]

	named = {name for name, _, _ in table}
	present = {path.stem: path for path in args.sources.glob("*.svg")}
	missing = sorted(named - present.keys())
	if missing:
		sys.exit(f"no source for {', '.join(missing)}: add {args.sources.name}/<Name>.svg "
				 f"(Scripts/IconBake/README.md says where one may come from..)")
	orphans = sorted(present.keys() - named)
	if orphans:
		sys.exit(f"no table entry uses these sources: {', '.join(o + '.svg' for o in orphans)}")

	digest = hashlib.sha256()
	for stem in sorted(present):
		digest.update(stem.encode("utf-8") + b"\0" + present[stem].read_bytes() + b"\0")
	sources_digest = digest.hexdigest()[:16]

	baked_all = []
	seen = set()
	for name, codepoint, face_name in table:
		key = (codepoint, face_name)
		if key in seen:
			sys.exit(f"{name}: U+{codepoint:04X} in {face_name} is baked twice. "
					 f"2 names share 1(codepoint, face)")
		seen.add(key)
		baked_all.append(bake(name, codepoint, face_name, present[name]))

	clipped = [b for b in baked_all if b.clipped > 0.0]
	if clipped:
		worst = max(clipped, key=lambda b: b.clipped)
		sys.exit(f"{len(clipped)} glyph(s) escape the baked box, worst {worst.name} by "
				 f"{worst.clipped:.1f} em units. Raise PAD_X/PAD_Y and re-run.")

	baked_all.sort(key=lambda b: (FACES.index(b.face), b.codepoint))
	source = emit(baked_all, sources_digest, header_digest)
	header = emit_header()
	header_path = OUTPUT.with_suffix(".h")

	if args.check:
		stale = []
		for path, text in ((OUTPUT, source), (header_path, header)):
			if not path.exists() or path.read_bytes() != text.encode("utf-8"):
				stale.append(path.relative_to(ROOT).as_posix())
		if stale:
			sys.exit("stale generated icon geometry: " + ", ".join(stale) +
					 "\nRun: python Scripts/bake_icon_glyphs.py")
		print(f"icon geometry up to date ({len(baked_all)} glyphs)")
		return

	OUTPUT.parent.mkdir(parents=True, exist_ok=True)
	write_bytes(OUTPUT, source)
	write_bytes(header_path, header)

	total_points = sum(len(b.points) for b in baked_all)
	total_verbs = sum(len(b.verbs) for b in baked_all)
	originals = sum(1 for b in baked_all if b.origin == "original")
	print(f"baked {len(baked_all)} glyph(s) ({len(baked_all) - originals} Font Awesome Free, {originals} original): "
		  f"{total_verbs} verb(s), {total_points} point(s) ({total_points * 8 + total_verbs} B of geometry)")
	print(f"  -> {OUTPUT.relative_to(ROOT).as_posix()}")
	print(f"  -> {header_path.relative_to(ROOT).as_posix()}")


if __name__ == "__main__":
	main()
