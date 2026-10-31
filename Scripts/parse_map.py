#!/usr/bin/env python3
"""description
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

GRAPHICS_DIRS = ("Source/Core/Private/Graphics", "Source/Core/Private/Graphics/Vulkan")

EXPECTED_ABSENT = {
	"msvc": ("PrimitiveMesh", "SkinAsset", "SkinnedMeshRenderer"),
	"gnu": ("PrimitiveMesh", "ShaderCompileQueue", "ShaderAssetAdopt", "MaterialKeywordToggles"),
}

VMA_EXPECTED = {"msvc": 141936, "gnu": 137888}
VMA_TOLERANCE = 256

SECTION_ROW = re.compile(r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]+)H\s+(\S+)\s+(\S+)")
SYMBOL_ROW = re.compile(r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+(\S+)\s+([0-9A-Fa-f]+)\s+(\S+)")

GNU_ROW = re.compile(r"^([0-9A-Fa-f]+) ([0-9A-Fa-f]+) +(\d+) (.*)$")
GNU_INPUT_INDENT = 8
GNU_SYMBOL_INDENT = 16


def graphics_translation_units(source):
	units = []
	for directory in GRAPHICS_DIRS:
		root = source / directory
		if not root.is_dir():
			continue
		for entry in sorted(root.glob("*.cpp")):
			units.append(entry.name)
	return units


def map_flag(map_path):
	driver = shutil.which("x86_64-w64-mingw32-clang++") or shutil.which("clang++")
	if driver is None:
		raise SystemExit("no clang++ on PATH")
	target = subprocess.run([driver, "-dumpmachine"], capture_output=True, text=True, check=True).stdout.strip()
	return f"-Wl,-Map={map_path}" if target.endswith("-gnu") else f"-Wl,/MAP:{map_path}"


def map_format(map_path):
	with open(map_path, "r", encoding="utf-8", errors="replace") as handle:
		head = handle.readline()
	return "gnu" if head.split()[:3] == ["Address", "Size", "Align"] else "msvc"


def parse_gnu_map(map_path):
	sections = {}
	current = None
	with open(map_path, "r", encoding="utf-8", errors="replace") as handle:
		handle.readline()
		for line in handle:
			row = GNU_ROW.match(line.rstrip("\r\n"))
			if not row:
				continue
			address, size, rest = int(row.group(1), 16), int(row.group(2), 16), row.group(4)
			indent = len(rest) - len(rest.lstrip(" "))
			name = rest.strip()
			if indent == 0:
				current = name
				sections.setdefault(current, (address, size, []))
			elif indent == GNU_INPUT_INDENT and current is not None:
				split = name.rfind(":(")
				owner = name[:split] if split >= 0 else name
				sections[current][2].append((address, size, owner))

	if ".text" not in sections:
		raise SystemExit(f"{map_path}: no .text output section. Is this ld.lld's map?")

	per_object = {}
	start, length, inputs = sections[".text"]
	end = start + length
	cursor = start
	previous = None
	attributed = 0
	for begin, size, owner in sorted(inputs, key=lambda entry: (entry[0], -entry[1])):
		if previous is not None and begin > cursor:
			pad = begin - cursor
			per_object[previous] = per_object.get(previous, 0) + pad
			attributed += pad
			cursor = begin
		claimed = max(begin + size, cursor) - max(begin, cursor)
		if claimed > 0:
			per_object[owner] = per_object.get(owner, 0) + claimed
			attributed += claimed
			cursor = begin + size
			previous = owner
	if previous is not None and end > cursor:
		per_object[previous] = per_object.get(previous, 0) + end - cursor
		attributed += end - cursor
	return per_object, length, attributed


def parse_map(map_path):
	if map_format(map_path) == "gnu":
		return parse_gnu_map(map_path)

	code_ranges = []
	symbols = []

	with open(map_path, "r", encoding="utf-8", errors="replace") as handle:
		for line in handle:
			section = SECTION_ROW.match(line)
			if section and section.group(5).upper() == "CODE":
				code_ranges.append((int(section.group(1), 16), int(section.group(2), 16), int(section.group(3), 16)))
				continue
			symbol = SYMBOL_ROW.match(line)
			if symbol:
				symbols.append((int(symbol.group(1), 16), int(symbol.group(2), 16), symbol.group(5)))

	if not code_ranges:
		raise SystemExit(f"{map_path}: no CODE section row. Is this an MSVC-style /MAP?")

	per_object = {}
	attributed = 0
	code_total = 0
	for index, start, length in code_ranges:
		code_total += length
		end = start + length
		inside = sorted((offset, name) for section, offset, name in symbols
						if section == index and start <= offset < end)
		for position, (offset, name) in enumerate(inside):
			following = inside[position + 1][0] if position + 1 < len(inside) else end
			if following <= offset:
				continue
			extent = following - offset
			per_object[name] = per_object.get(name, 0) + extent
			attributed += extent

	return per_object, code_total, attributed


def text_virtual_size(exe_path):
	with open(exe_path, "rb") as handle:
		data = handle.read()
	pe = struct.unpack_from("<I", data, 0x3C)[0]
	sections = struct.unpack_from("<H", data, pe + 6)[0]
	optional = struct.unpack_from("<H", data, pe + 20)[0]
	table = pe + 24 + optional
	for index in range(sections):
		entry = table + index * 40
		name = data[entry:entry + 8].rstrip(b"\0").decode("ascii", "replace")
		if name == ".text":
			return struct.unpack_from("<I", data, entry + 8)[0]
	return 0


def render_template(source, destination, module_name):
	text = source.read_text(encoding="utf-8")
	destination.write_text(text.replace("{{MODULE_NAME}}", module_name), encoding="utf-8", newline="\n")


def link_probe(out_dir):
	sdk = REPO / "ShalltearSDK" / "Static"
	if not sdk.is_dir():
		raise SystemExit("ShalltearSDK/Static is missing. Run python Scripts/build_sdk.py")

	out_dir.mkdir(parents=True, exist_ok=True)
	templates = REPO / "EngineAssets" / "Templates"
	render_template(templates / "PlayerProject.cmake.template", out_dir / "CMakeLists.txt", "Probe")
	render_template(templates / "PlayerProjectStub.cpp.template", out_dir / "GameStub.cpp", "Probe")

	map_path = (out_dir / "Probe.map").resolve()
	binaries = (out_dir / "bin").resolve()
	configure = [
		"cmake", "-S", str(out_dir), "-B", str(out_dir), "-G", "Ninja",
		"-DCMAKE_BUILD_TYPE=Release",
		"-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-clang++",
		"-DCMAKE_C_COMPILER=x86_64-w64-mingw32-clang",
		f"-DShalltear_ROOT={sdk}",
		f"-DSHALLTEAR_PROJECT_ROOT={out_dir}",
		"-DSHALLTEAR_PLAYER_TARGET=Windows",
		"-DSHALLTEAR_PLAYER_ENTRY=PlayerMain.cpp",
		f"-DSHALLTEAR_PLAYER_OUTPUT_DIRECTORY={binaries}",
		"-DSHALLTEAR_PLAYER_OUTPUT_NAME=Probe",
		"-DSHALLTEAR_PLAYER_DEBUG_SYMBOLS=OFF",
		f'-DCMAKE_EXE_LINKER_FLAGS={map_flag(map_path)}',
	]
	subprocess.run(configure, check=True, cwd=REPO)
	subprocess.run(["cmake", "--build", str(out_dir), "--parallel"], check=True, cwd=REPO)
	return map_path, binaries / "Probe.exe"


def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--map", type=Path, help="an existing /MAP file to parse")
	parser.add_argument("--link", action="store_true", help="render, configure and link the probe player first")
	parser.add_argument("--out", type=Path, default=REPO / "Library" / "retail", help="where --link works")
	parser.add_argument("--check", action="store_true", help="fail unless the instrument validates")
	parser.add_argument("--all", action="store_true", help="list all objects (not only the graphics set)")
	parser.add_argument("--exe", type=Path, help="the linked player beside a --map, for the .text VirtualSize")
	parser.add_argument("--source", type=Path, default=REPO, help="checkout to count graphics TUs in")
	parser.add_argument("--sdk", type=Path, help="the static SDK the map was linked with (default: <source>/ShalltearSDK/Static)")
	args = parser.parse_args()
	if args.sdk is None:
		args.sdk = args.source / "ShalltearSDK" / "Static"

	exe_path = args.exe
	if args.link:
		map_path, exe_path = link_probe(args.out)
	elif args.map:
		map_path = args.map
	else:
		raise SystemExit("pass --link or --map")

	kind = map_format(map_path)
	per_object, code_total, attributed = parse_map(map_path)

	units = graphics_translation_units(args.source)
	present = {name: per_object.get(name + ".obj", 0) for name in units}
	if not units:
		raise SystemExit(f"no graphics TUs under {args.source}. Glob examined nothing")
	for name in units:
		if present[name] == 0:
			for key, value in per_object.items():
				if key.endswith(name + ".obj") and key[-len(name) - 5:-len(name) - 4] in (":", "/", "\\", "("):
					present[name] = value
					break

	total = sum(present.values())
	vma = present.get("VmaUsage.cpp", 0)
	published = total - vma

	print(f"map:        {map_path} ({kind})")
	print(f"source:     {args.source}")
	print(f"CODE span:  {code_total} B")
	print(f"attributed: {attributed} B ({attributed / code_total * 100:.2f}%)")
	if exe_path and exe_path.exists():
		print(f".text VirtualSize: {text_virtual_size(exe_path)} B")
	print()
	print(f"graphics TUs found: {sum(1 for value in present.values() if value)} of {len(units)}")
	for name in sorted(present, key=lambda key: -present[key]):
		marker = "  (absent)" if present[name] == 0 else ""
		print(f"  {present[name]:>9} {name}{marker}")
	print()
	print(f"graphics .text                 {total} B")
	print(f"less VmaUsage.cpp ({vma})  {published} B   <- the published figure")

	if args.all:
		print()
		for name in sorted(per_object, key=lambda key: -per_object[key])[:40]:
			print(f"  {per_object[name]:>9} {name}")

	if args.check:
		failures = []
		expected = VMA_EXPECTED[kind]
		if abs(vma - expected) > VMA_TOLERANCE:
			failures.append(f"VmaUsage.cpp is {vma}, outside {expected} +/- {VMA_TOLERANCE}. Not the same instrument")
		elif vma != expected:
			print(f"\nnote: VmaUsage.cpp is {vma}, {vma - expected:+} against recorded {expected}. ICF, not a different instrument")
		absent_checked = 0
		for name in EXPECTED_ABSENT[kind]:
			if name + ".cpp" not in present:
				print(f"note: {name}.cpp isn't in {args.source} (its absence proves nothing)")
				continue
			absent_checked += 1
			if present[name + ".cpp"]:
				failures.append(f"{name}.cpp was expected absent from a content-free player, found {present[name + '.cpp']} B")
		if absent_checked == 0:
			failures.append("none of expected-absent TUs exist in this checkout")
		if kind == "gnu":
			counts = {}
			for archive in sorted((args.sdk / "lib").glob("*.a")):
				listing = subprocess.run(["llvm-ar", "t", str(archive)], capture_output=True, text=True, check=True).stdout
				for member in listing.split():
					counts[member] = counts.get(member, 0) + 1
			shared = [name for name in units if counts.get(name + ".obj", 0) > 1]
			if not counts.get("RenderSystem.cpp.obj"):
				failures.append(f"no archive under {args.sdk / 'lib'} holds RenderSystem.cpp.obj. Member scan examined nothing")
			elif shared:
				failures.append(f"graphics TUs sharing an archive member name (the map can't tell them apart): {shared}")
		if abs(attributed - code_total) > 0:
			failures.append(f"attribution is {attributed}/{code_total}, not 100.00%")
		if failures:
			for failure in failures:
				print(f"FAIL: {failure}", file=sys.stderr)
			return 1
		print(f"\ninstrument validated ({kind}): VmaUsage in band, {absent_checked} expected-absent TUs absent, 100.00% attributed")
	return 0


if __name__ == "__main__":
	sys.exit(main())
