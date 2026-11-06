import sys
from pathlib import Path

try:
	from fontTools.fontBuilder import FontBuilder
	from fontTools.pens.ttGlyphPen import TTGlyphPen
	from fontTools.ttLib.tables.TupleVariation import TupleVariation
except ImportError:
	sys.exit("fontTools is required: python -m pip install fonttools")

OUT = Path(__file__).resolve().parent.parent / "Source" / "Tests" / "Fixtures" / "Fonts"

UPEM = 1000
GLYPHS = [".notdef", "space", "I", "question"]
CMAP = {0x20: "space", 0x49: "I", 0x3F: "question"}
STEM = (50, 150)
HEIGHT = 700


def rect(x0, x1, y0, y1):
	pen = TTGlyphPen(None)
	pen.moveTo((x0, y0))
	pen.lineTo((x0, y1))
	pen.lineTo((x1, y1))
	pen.lineTo((x1, y0))
	pen.closePath()
	return pen.glyph()


def empty():
	return TTGlyphPen(None).glyph()


def build(name, weight, italic, advance, axes=None, deltas=None):
	fb = FontBuilder(UPEM, isTTF=True)
	fb.setupGlyphOrder(GLYPHS)
	fb.setupCharacterMap(CMAP)
	fb.setupGlyf({
		".notdef": rect(50, 450, 0, HEIGHT),
		"space": empty(),
		"I": rect(STEM[0], STEM[1], 0, HEIGHT),
		"question": rect(50, 250, 0, HEIGHT),
	})
	fb.setupHorizontalMetrics({
		".notdef": (500, 50),
		"space": (250, 0),
		"I": (advance, STEM[0]),
		"question": (300, 50),
	})
	fb.setupHorizontalHeader(ascent=800, descent=-200)
	style = ("Bold " if weight >= 700 else "") + ("Italic" if italic else "")
	style = style.strip() or "Regular"
	fb.setupNameTable({"familyName": "Shalltear Fixture " + name, "styleName": style})
	fs_selection = (0x01 if italic else 0) | (0x20 if weight >= 700 else 0) | (0x40 if not italic and weight < 700 else 0)
	fb.setupOS2(usWeightClass=weight, fsSelection=fs_selection, sTypoAscender=800, sTypoDescender=-200,
				sTypoLineGap=0, usWinAscent=800, usWinDescent=200)
	fb.setupHead(macStyle=(0x01 if weight >= 700 else 0) | (0x02 if italic else 0))
	fb.setupPost()
	if axes:
		fb.setupFvar(axes, [])
		variations = {}
		for glyph, tuples in (deltas or {}).items():
			variations[glyph] = [TupleVariation(region, [(0, 0)] * 4 + [(0, 0), (dx, 0), (0, 0), (0, 0)])
								 for region, dx in tuples]
		fb.setupGvar(variations)
	fb.save(str(OUT / (name + ".ttf")))


def main():
	OUT.mkdir(parents=True, exist_ok=True)
	for name, weight, italic in [("Light", 300, False), ("Regular", 400, False), ("Medium", 500, False),
								 ("Bold", 700, False), ("Italic", 400, True), ("BoldItalic", 700, True)]:
		build(name, weight, italic, weight + (50 if italic else 0))

	build("WeightAxis", 100, False, 100, axes=[("wght", 100, 100, 900, "Weight")],
		  deltas={"I": [({"wght": (0.0, 1.0, 1.0)}, 800)]})
	build("ItalicAxis", 400, False, 400, axes=[("wght", 100, 400, 900, "Weight"), ("ital", 0, 0, 1, "Italic")],
		  deltas={"I": [({"wght": (-1.0, -1.0, 0.0)}, -300), ({"wght": (0.0, 1.0, 1.0)}, 500),
						({"ital": (0.0, 1.0, 1.0)}, 100)]})
	build("SlantAxis", 400, False, 400, axes=[("slnt", -20, 0, 0, "Slant")],
		  deltas={"I": [({"slnt": (-1.0, -1.0, 0.0)}, 200)]})
	build("OpticalSizeAxis", 400, False, 220, axes=[("opsz", 10, 10, 100, "Optical Size")],
		  deltas={"I": [({"opsz": (0.0, 1.0, 1.0)}, 180)]})
	print(f"wrote 10 fonts to {OUT}")


if __name__ == "__main__":
	main()
