#!/usr/bin/env python3
"""Strip comments from packaged SDK sources and stamp the license banner
"""

import os
import sys
import pathlib

STRIPPABLE_SUFFIXES = {".h", ".hpp", ".inl", ".cpp"}


LICENSE_ID = "MIT"
COPYRIGHT_HOLDER = "mewiof"
COPYRIGHT_YEAR = "2026"

DIVIDER = "// " + "=" * 64

BANNER = f"""\
{DIVIDER}
// SHALLTEAR ENGINE SDK
{DIVIDER}
//
// Copyright (c) {COPYRIGHT_YEAR} {COPYRIGHT_HOLDER}
// SPDX-License-Identifier: {LICENSE_ID}
//
// Distributed under the MIT License. The full text is in LICENSE.txt,
// which accompanies the distribution this file was received with.
{DIVIDER}
"""

BANNER_MARKER = "// SHALLTEAR ENGINE SDK"


RAW_PREFIXES = {"R", "LR", "uR", "UR", "u8R"}
STRING_PREFIXES = {"L", "u", "U", "u8"}


class StripError(Exception):
	pass


KEPT_LINE_COMMENT_PREFIXES = ("// IWYU pragma:",)


def _is_kept_line_comment(text, i):
	return text.startswith(KEPT_LINE_COMMENT_PREFIXES, i)


def _skip_line_comment(text, i):
	j = i + 2
	n = len(text)
	while j < n:
		if text[j] == "\\" and j + 1 < n and text[j + 1] == "\n":
			j += 2
			continue
		if text[j] == "\n":
			break
		j += 1
	return j


def _skip_block_comment(text, i):
	end = text.find("*/", i + 2)
	if end < 0:
		raise StripError("unterminated /* comment")
	return end + 2


def _copy_quoted(text, i):
	quote = text[i]
	j = i + 1
	n = len(text)
	while j < n:
		if text[j] == "\\":
			j += 2
			continue
		if text[j] == quote:
			return j + 1
		if text[j] == "\n":
			raise StripError("unterminated %s literal" % quote)
		j += 1
	raise StripError("unterminated %s literal" % quote)


def _copy_raw_string(text, i):
	open_paren = text.find("(", i + 1)
	if open_paren < 0:
		raise StripError("malformed raw string literal")
	closer = ")" + text[i + 1:open_paren] + '"'
	end = text.find(closer, open_paren + 1)
	if end < 0:
		raise StripError("unterminated raw string literal")
	return end + len(closer)


def _copy_number(text, i):
	j = i
	n = len(text)
	while j < n:
		c = text[j]
		if c.isalnum() or c == "_" or c == ".":
			j += 1
		elif c == "'" and j + 1 < n and (text[j + 1].isalnum() or text[j + 1] == "_"):
			j += 2
		elif c in "+-" and j > i and text[j - 1] in "eEpP":
			j += 1
		else:
			break
	return j


def scan(text):
	i = 0
	n = len(text)
	while i < n:
		c = text[i]
		if c.isalpha() or c == "_":
			j = i
			while j < n and (text[j].isalnum() or text[j] == "_"):
				j += 1
			if j < n and text[j] == '"' and text[i:j] in RAW_PREFIXES:
				end = _copy_raw_string(text, j)
				yield "literal", i, end
				i = end
			else:
				i = j
		elif c.isdigit():
			i = _copy_number(text, i)
		elif c == '"' or c == "'":
			j = _copy_quoted(text, i)
			yield "literal", i, j
			i = j
		elif c == "/" and i + 1 < n and text[i + 1] == "/":
			j = _skip_line_comment(text, i)
			yield "line", i, j
			i = j
		elif c == "/" and i + 1 < n and text[i + 1] == "*":
			j = _skip_block_comment(text, i)
			yield "block", i, j
			i = j
		else:
			i += 1


def strip_comments(text):
	out = []
	last = 0
	for kind, start, end in scan(text):
		if kind == "literal":
			continue
		out.append(text[last:start])
		if kind == "line" and _is_kept_line_comment(text, start):
			out.append(text[start:end])
		elif kind == "block":
			out.append(" ")
		last = end
	out.append(text[last:])
	return "".join(out)


def tidy(text):
	kept = []
	for line in text.split("\n"):
		line = line.rstrip(" \t")
		if line == "":
			if kept and kept[-1].endswith("\\"):
				kept.append(line)
			continue
		if line.strip() == "\\":
			continue
		kept.append(line)
	while kept and kept[-1] == "":
		kept.pop()
	return "\n".join(kept) + "\n"


def transform(source_bytes):
	try:
		text = source_bytes.decode("utf-8")
	except UnicodeDecodeError as exc:
		raise StripError("not valid UTF-8: %s" % exc)
	if text.startswith("﻿"):
		raise StripError("byte-order mark (repository is BOM-free)")
	if BANNER_MARKER in text:
		return source_bytes
	text = text.replace("\r\n", "\n")
	stripped = tidy(strip_comments(text))
	for number, line in enumerate(stripped.split("\n"), 1):
		if line.startswith(" "):
			raise StripError("line %d indents with a space (style is tabs)" % number)
		if line != line.rstrip():
			raise StripError("line %d ends in whitespace" % number)
		if "\r" in line:
			raise StripError("line %d has a carriage return" % number)
	result = (BANNER + "\n" + stripped).encode("utf-8")
	if result.decode("utf-8").encode("utf-8") != result:
		raise StripError("output didn't survive a UTF-8 round trip")
	return result


def strippable(path):
	return path.is_file() and path.suffix.lower() in STRIPPABLE_SUFFIXES


def run(sources, out_dir):
	before = 0
	after = 0
	count = 0
	failures = []
	for source in sources:
		root = pathlib.Path(source).resolve()
		if not root.is_dir():
			failures.append("%s: not a directory" % root)
			continue
		for path in sorted(root.rglob("*")):
			if not strippable(path):
				continue
			raw = path.read_bytes()
			try:
				result = transform(raw)
			except StripError as exc:
				failures.append("%s: %s" % (path, exc))
				continue
			if out_dir is None:
				target = path
			else:
				target = pathlib.Path(out_dir).resolve() / path.relative_to(root)
				target.parent.mkdir(parents=True, exist_ok=True)
			if not target.exists() or target.read_bytes() != result:
				target.write_bytes(result)
			before += len(raw)
			after += len(result)
			count += 1
	if failures:
		for failure in failures:
			print("strip_sdk_source: %s" % failure, file=sys.stderr)
		return 1
	print("strip_sdk_source: %d file(s), %d B -> %d B (%.1f%% removed)"
		  % (count, before, after, 0.0 if before == 0 else 100.0 * (before - after) / before))
	return 0


SELF_TEST_CASES = [
	("#pragma once\n// gone\nint a; // gone\n", "#pragma once\nint a;\n"),
	("// IWYU pragma: begin_exports\n#include \"a.h\"\n// IWYU pragma: end_exports\n",
	 "// IWYU pragma: begin_exports\n#include \"a.h\"\n// IWYU pragma: end_exports\n"),
	('#include "a.h" // IWYU pragma: export\n', '#include "a.h" // IWYU pragma: export\n'),
	("// see IWYU pragma: export below\nint a;\n", "int a;\n"),
	('const char* p = "http://x // y /* z";\n', 'const char* p = "http://x // y /* z";\n'),
	('auto s = R"(// not a comment /* nor this */)";\n', 'auto s = R"(// not a comment /* nor this */)";\n'),
	('auto s = R"tag(a)" b)tag";\n', 'auto s = R"tag(a)" b)tag";\n'),
	('auto s = u8R"(//x)";\n', 'auto s = u8R"(//x)";\n'),
	("char c = '\\''; // gone\n", "char c = '\\'';\n"),
	("char c = '/'; /* gone */ int q;\n", "char c = '/';   int q;\n"),
	("int n = 1'000'000; // gone\n", "int n = 1'000'000;\n"),
	("double d = 1.0e+5; // gone\n", "double d = 1.0e+5;\n"),
	("void f(int /*x*/, int /*y*/);\n", "void f(int  , int  );\n"),
	("#define M \\\n\t/* c */ \\\n\ta\n", "#define M \\\n\ta\n"),
	("#define M a \\\n\nint b;\n", "#define M a \\\n\nint b;\n"),
	("#define M \\\n\t/* c */\nint b;\n", "#define M \\\n\nint b;\n"),
	("#define M(x) \\\n\tf(x); /* c */ \\\n\tg(x)\n", "#define M(x) \\\n\tf(x);   \\\n\tg(x)\n"),
	("#define M a /* c */\\\n\tb\n", "#define M a  \\\n\tb\n"),
	("int a;\n\n\n\n\nint b;\n", "int a;\nint b;\n"),
	("int a;\n\nint b;\n", "int a;\nint b;\n"),
	("/* a\n b */\n#pragma once\n", "#pragma once\n"),
	('auto w = L"// x";\n', 'auto w = L"// x";\n'),
	('auto w = u8"/* x */";\n', 'auto w = u8"/* x */";\n'),
	("int a = b / c / d; // gone\n", "int a = b / c / d;\n"),
	('const char* SERVER = "x"; // R" is not a prefix here\n', 'const char* SERVER = "x";\n'),
]

SELF_TEST_REFUSALS = [
	'auto s = "unterminated;\n',
	"int a; /* never closed\n",
]

SELF_TEST_SHAPE_REFUSALS = [
	b"#pragma once\n    int x;\n",
	b"\xef\xbb\xbf#pragma once\n",
	b"#pragma once\n\xff\xfe\n",
]


SELF_TEST_SPANS = [
	('a /* b */ "c" // d\n', [("block", 2, 9), ("literal", 10, 13), ("line", 14, 18)]),
	("int n = 1'0; char c = '/'; // x \\\ny\nz", [("literal", 22, 25), ("line", 27, 35)]),
	('auto s = u8R"(//)"; L"/*"', [("literal", 9, 18), ("literal", 21, 25)]),
]


def self_test():
	bad = 0
	for source, expected in SELF_TEST_SPANS:
		actual = list(scan(source))
		if actual != expected:
			bad += 1
			print("strip_sdk_source: FAILED spans\n  in   %r\n  want %r\n  got  %r"
				  % (source, expected, actual), file=sys.stderr)
	for source, expected in SELF_TEST_CASES:
		actual = tidy(strip_comments(source))
		if actual != expected:
			bad += 1
			print("strip_sdk_source: FAILED\n  in   %r\n  want %r\n  got  %r"
				  % (source, expected, actual), file=sys.stderr)
	for source in SELF_TEST_REFUSALS:
		try:
			strip_comments(source)
		except StripError:
			continue
		bad += 1
		print("strip_sdk_source: FAILED to reject %r" % source, file=sys.stderr)
	for raw in SELF_TEST_SHAPE_REFUSALS:
		try:
			transform(raw)
		except StripError:
			continue
		bad += 1
		print("strip_sdk_source: FAILED to reject %r" % raw, file=sys.stderr)
	stamped = transform(b"#pragma once\n// gone\n").decode("utf-8")
	if BANNER_MARKER not in stamped or f"SPDX-License-Identifier: {LICENSE_ID}" not in stamped:
		bad += 1
		print("strip_sdk_source: FAILED to stamp the banner", file=sys.stderr)
	if transform(stamped.encode("utf-8")) != stamped.encode("utf-8"):
		bad += 1
		print("strip_sdk_source: FAILED to be idempotent", file=sys.stderr)
	if bad == 0:
		print("strip_sdk_source: self-test passed (%d case(s))"
			  % (len(SELF_TEST_SPANS) + len(SELF_TEST_CASES) + len(SELF_TEST_REFUSALS)
				 + len(SELF_TEST_SHAPE_REFUSALS) + 2))
	return 1 if bad else 0


def main(argv):
	if argv and argv[0] == "--self-test":
		return self_test()
	out_dir = None
	sources = []
	i = 0
	while i < len(argv):
		if argv[i] == "--out":
			if i + 1 >= len(argv):
				print(__doc__)
				return 2
			out_dir = argv[i + 1]
			i += 2
			continue
		sources.append(argv[i])
		i += 1
	if not sources:
		print(__doc__)
		return 2
	return run(sources, out_dir)


if __name__ == "__main__":
	sys.exit(main(sys.argv[1:]))
