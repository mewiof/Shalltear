import os
import re
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

ROOT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
SOURCE_SVG = os.path.join(ROOT_DIR, 'Source', 'EditorExecutable', 'Shalltear.svg')
OUTPUT_ICO = os.path.join(ROOT_DIR, 'Source', 'EditorExecutable', 'Shalltear.ico')

SVG_NS = '{http://www.w3.org/2000/svg}'

ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]

SUPERSAMPLE = 8


def parse_transform(text):
	matrix = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
	if not text:
		return matrix
	for name, args in re.findall(r'([a-zA-Z]+)\s*\(([^)]*)\)', text):
		values = [float(v) for v in re.split(r'[\s,]+', args.strip()) if v]
		if name == 'translate':
			step = (1.0, 0.0, 0.0, 1.0, values[0], values[1] if len(values) > 1 else 0.0)
		elif name == 'scale':
			sx = values[0]
			sy = values[1] if len(values) > 1 else sx
			step = (sx, 0.0, 0.0, sy, 0.0, 0.0)
		else:
			raise SystemExit(f"make_app_icon: transform '{name}' is outside the supported subset")
		matrix = multiply(matrix, step)
	return matrix


def multiply(outer, inner):
	a1, b1, c1, d1, e1, f1 = outer
	a2, b2, c2, d2, e2, f2 = inner
	return (
		a1 * a2 + c1 * b2,
		b1 * a2 + d1 * b2,
		a1 * c2 + c1 * d2,
		b1 * c2 + d1 * d2,
		a1 * e2 + c1 * f2 + e1,
		b1 * e2 + d1 * f2 + f1,
	)


def apply(matrix, point):
	a, b, c, d, e, f = matrix
	x, y = point
	return (a * x + c * y + e, b * x + d * y + f)


def parse_polygon(d):
	if re.search(r'[CcSsQqTtAaHhVv]', d):
		raise SystemExit('make_app_icon: the mark grew a curve (this renderer only draws straight edges)')
	points = []
	for command, args in re.findall(r'([MLZmlz])([^MLZmlz]*)', d):
		values = [float(v) for v in re.split(r'[\s,]+', args.strip()) if v]
		if command in 'Zz':
			continue
		if command in 'ml' and points:
			raise SystemExit('make_app_icon: relative path commands are outside the supported subset')
		for i in range(0, len(values), 2):
			points.append((values[i], values[i + 1]))
	if len(points) < 3:
		raise SystemExit(f"make_app_icon: a path resolved to {len(points)} point(s)")
	return points


class Face:

	def __init__(self, points, opacity, hole):
		self.points = points
		self.opacity = opacity
		self.hole = hole


def collect(svg):
	tile = None
	tile_radius = 0.0
	masks = {}
	faces = []

	def walk(node, matrix, opacity, mask_id):
		nonlocal tile, tile_radius
		matrix = multiply(matrix, parse_transform(node.get('transform')))
		if node.get('fill-opacity') is not None:
			opacity = float(node.get('fill-opacity'))
		if node.get('mask'):
			reference = re.fullmatch(r'url\(#([^)]+)\)', node.get('mask'))
			if not reference:
				raise SystemExit("make_app_icon: a mask attribute isn't a local url(#id) reference")
			mask_id = reference.group(1)

		tag = node.tag[len(SVG_NS):] if node.tag.startswith(SVG_NS) else node.tag
		if tag == 'mask':
			for child in node:
				if child.tag == SVG_NS + 'path':
					masks[node.get('id')] = (parse_polygon(child.get('d')),
											 parse_transform(child.get('transform')))
			return
		if tag == 'rect':
			if tile is not None:
				raise SystemExit('make_app_icon: a 2nd rect appeared (the tile should be the only one)')
			tile = node.get('fill')
			tile_radius = float(node.get('rx', 0.0))
			return
		if tag == 'path':
			hole = None
			if mask_id:
				if mask_id not in masks:
					raise SystemExit(f"make_app_icon: mask '{mask_id}' is referenced before it is defined")
				points, hole_matrix = masks[mask_id]
				hole = [apply(multiply(matrix, hole_matrix), p) for p in points]
			faces.append(Face([apply(matrix, p) for p in parse_polygon(node.get('d'))], opacity, hole))
			return
		for child in node:
			walk(child, matrix, opacity, mask_id)

	walk(svg, (1.0, 0.0, 0.0, 1.0, 0.0, 0.0), 1.0, None)
	if tile is None or not faces:
		raise SystemExit('make_app_icon: the document produced no tile or no faces')
	return tile, tile_radius, faces


def render(size, view_box, tile, tile_radius, faces):
	scale = size * SUPERSAMPLE / view_box
	big = size * SUPERSAMPLE

	image = Image.new('RGBA', (big, big), (0, 0, 0, 0))
	ImageDraw.Draw(image).rounded_rectangle([0, 0, big - 1, big - 1],
											radius=tile_radius * scale, fill=tile)

	for face in faces:
		coverage = Image.new('L', (big, big), 0)
		draw = ImageDraw.Draw(coverage)
		draw.polygon([(x * scale, y * scale) for x, y in face.points], fill=255)
		if face.hole:
			draw.polygon([(x * scale, y * scale) for x, y in face.hole], fill=0)
		if face.opacity < 1.0:
			coverage = coverage.point(lambda v, o=face.opacity: int(v * o))
		image.paste(Image.new('RGBA', (big, big), (255, 255, 255, 255)), (0, 0), coverage)

	return image.resize((size, size), Image.LANCZOS)


def main():
	if not os.path.exists(SOURCE_SVG):
		raise SystemExit(f"make_app_icon: {SOURCE_SVG} is missing")

	svg = ET.parse(SOURCE_SVG).getroot()
	box = [float(v) for v in re.split(r'[\s,]+', svg.get('viewBox').strip())]
	if box[0] != 0.0 or box[1] != 0.0 or box[2] != box[3]:
		raise SystemExit('make_app_icon: the mark is no longer a square view box at the origin')

	tile, tile_radius, faces = collect(svg)
	print(f"{os.path.relpath(SOURCE_SVG, ROOT_DIR)}: {len(faces)} face(s) on a {tile} tile, "
		  f"{sum(1 for f in faces if f.hole)} of them masked")

	images = [render(size, box[2], tile, tile_radius, faces) for size in ICON_SIZES]
	images[-1].save(OUTPUT_ICO, format='ICO',
					sizes=[(size, size) for size in ICON_SIZES],
					append_images=images[:-1])
	print(f"wrote {os.path.relpath(OUTPUT_ICO, ROOT_DIR)} "
		  f"({os.path.getsize(OUTPUT_ICO):,} B, sizes {', '.join(str(s) for s in ICON_SIZES)})")


if __name__ == '__main__':
	sys.exit(main())
