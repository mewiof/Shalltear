import json
import os
import re

SEPARATORS = "_-"
STYLES = ("Pascal", "camel", "CONST", "snake", "kebab", "lower")

IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

CASE_BEARING = ("Pascal", "camel")


class NamingError(Exception):
	pass


class Dictionary:
	def __init__(self, data):
		self.initialisms = {e.upper(): e.upper() for e in data.get("initialisms", [])}
		self.fixed = {e.upper(): e for e in data.get("fixed", [])}
		self.split = {k.lower(): v for k, v in data.get("split", {}).items()}
		self.axis = set(data.get("axisSymbols", "").upper())
		self.binding = data.get("binding", {})
		kept = {v: v for k, v in data.get("prefixes", {}).items() if not k.startswith("_")}
		kept.update(data.get("legacyPrefixes", {}))
		self.prefixes = sorted(kept.items(), key=lambda kv: len(kv[0]), reverse=True)
		self._prepass_entries = sorted(
			set(list(self.fixed.values()) + self.digit_entries()), key=len, reverse=True
		)
		self._recase_cache = {}
		self._validate()

	def _validate(self):
		clash = set(self.initialisms) & set(self.fixed)
		if clash:
			raise NamingError("entry is both INITIALISM and FIXED: %s" % sorted(clash))
		keys = set(self.initialisms) | set(self.fixed)
		for k in keys:
			if k.endswith("S") and k[:-1] in keys:
				raise NamingError("%s is %s plus a plural (would double-parse)" % (k, k[:-1]))

	def lookup(self, token):
		u = token.upper()
		if u in self.fixed:
			return self.fixed[u], "FIXED"
		if u in self.initialisms:
			return self.initialisms[u], "INITIALISM"
		return None

	def digit_entries(self):
		return [e for e in list(self.initialisms) + list(self.fixed) if any(c.isdigit() for c in e)]


def load(path=None):
	if path is None:
		path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "naming-dictionary.json")
	with open(path, "rb") as f:
		return Dictionary(json.loads(f.read().decode("utf-8")))


def norm(path):
	return os.path.normcase(os.path.abspath(path)).replace(os.sep, "/")


def contains(root, path):
	stem = norm(root).rstrip("/")
	npath = norm(path)
	return npath == stem or npath.startswith(stem + "/")


def _strip_prefix(name, dic):
	for found, keep in dic.prefixes:
		if found and name.startswith(found) and len(name) > len(found):
			return keep, name[len(found):]
	return "", name


def _is_boundary_start(name, i):
	if i == 0:
		return True
	prev, cur = name[i - 1], name[i]
	if prev in SEPARATORS:
		return True
	if prev.islower() and cur.isupper():
		return True
	if prev.isalpha() != cur.isalpha():
		return True
	return False


def _prepass(name, dic):
	entries = dic._prepass_entries
	out, i, pending = [], 0, ""
	while i < len(name):
		hit = None
		for e in entries:
			if name[i:i + len(e)].upper() != e.upper():
				continue
			if not _is_boundary_start(name, i):
				continue
			end = i + len(e)
			nxt = name[end] if end < len(name) else ""
			if nxt.islower():
				continue
			if nxt and (nxt.isupper() or nxt.isdigit()) and i > 0 and name[i - 1].isupper():
				continue
			hit = e
			break
		if hit:
			if pending:
				out.append((pending, None))
				pending = ""
			out.append((name[i:i + len(hit)], hit))
			i += len(hit)
		else:
			pending += name[i]
			i += 1
	if pending:
		out.append((pending, None))
	return out


def _split_segment(seg, dic):
	tokens, i = [], 0
	while i < len(seg):
		c = seg[i]
		if c in SEPARATORS:
			i += 1
			continue
		if c.isupper():
			j = i
			while j < len(seg) and seg[j].isupper():
				j += 1
			if j < len(seg) and seg[j].islower():
				tail = seg[j + 1] if j + 1 < len(seg) else ""
				if seg[j] == "s" and (tail == "" or tail.isupper() or tail in SEPARATORS):
					tokens.append(seg[i:j + 1])
					i = j + 1
					continue
				if j - i > 1:
					tokens.append(seg[i:j - 1])
					i = j - 1
				k = i + 1
				while k < len(seg) and (seg[k].islower() or seg[k].isdigit()):
					k += 1
				tokens.append(seg[i:k])
				i = k
			else:
				k = j
				while k < len(seg) and seg[k].isdigit():
					k += 1
				tokens.append(seg[i:k])
				i = k
		else:
			k = i
			while k < len(seg) and (seg[k].islower() or seg[k].isdigit()):
				k += 1
			if k == i:
				k = i + 1
			tokens.append(seg[i:k])
			i = k
	bound = []
	for t in tokens:
		if bound and t and t[0].isdigit():
			bound[-1] += t
		else:
			bound.append(t)
	out = []
	for t in bound:
		out.extend(dic.split.get(t.lower(), [t]))
	return out


def tokenize(name, dic):
	if not IDENTIFIER.match(name):
		raise NamingError("%r isn't an identifier" % name)
	prefix, core = _strip_prefix(name, dic)
	tokens = []
	for text, entry in _prepass(core, dic):
		if entry is not None:
			tokens.append(text)
		else:
			tokens.extend(_split_segment(text, dic))
	return prefix, [t for t in tokens if t]


def _parse_run(run, dic):
	results = []

	def walk(rest, acc):
		if not rest:
			if len(acc) >= 2:
				results.append(list(acc))
			return
		for n in range(len(rest), 0, -1):
			hit = dic.lookup(rest[:n])
			if hit:
				acc.append(hit[0])
				walk(rest[n:], acc)
				acc.pop()

	walk(run, [])
	return results


def resolve(token, dic, style, allow_axis=True, delimited=False):
	hit = dic.lookup(token)
	if hit:
		return [(hit[0], hit[1], False)]

	stem = token.rstrip("0123456789")
	digits = token[len(stem):]
	if digits:
		hit = dic.lookup(stem)
		if hit:
			return [(hit[0] + digits, hit[1], False)]

	if len(token) > 1 and token[-1] in "sS":
		body = token[:-1]
		hit = dic.lookup(body)
		if hit:
			return [(hit[0], hit[1], True)]
		bstem = body.rstrip("0123456789")
		bdigits = body[len(bstem):]
		if bdigits:
			hit = dic.lookup(bstem)
			if hit:
				return [(hit[0] + bdigits, hit[1], True)]

	for body, plural in ((token, False), (token[:-1], True)):
		if plural and not (len(token) > 1 and token[-1] in "sS"):
			continue
		if not (body.isupper() and body.isalpha()):
			continue
		parses = _parse_run(body, dic)
		if len(parses) > 1:
			raise NamingError("%r has %d parses. Rename it" % (token, len(parses)))
		if len(parses) == 1:
			run = parses[0]
			return [(e, "INITIALISM", plural and i == len(run) - 1) for i, e in enumerate(run)]

	if allow_axis and dic.axis and len(token) >= 2 and token.isalpha() and token.isupper() \
			and all(c in dic.axis for c in token):
		return [(token, "INITIALISM", False)]

	if style in CASE_BEARING and not delimited:
		run = re.match(r"[A-Z]{3,}", token)
		if run and run.group() == token.rstrip("s"):
			raise NamingError("unregistered all-caps run %r: register it or rename" % token)
	return [(token, "WORD", False)]


def _pascal_token(canonical, kind, first):
	if kind == "FIXED" and not first and canonical[:1].islower():
		return canonical.upper()
	if kind in ("INITIALISM", "FIXED"):
		return canonical
	return canonical[0].upper() + canonical[1:].lower()


def _camel_first(canonical, kind):
	if kind == "INITIALISM":
		return canonical.lower()
	if kind == "FIXED" and canonical[:1].isupper():
		return canonical.lower()
	return canonical[0].lower() + canonical[1:]


def recase(name, style, dic, migration=False):
	if style not in STYLES:
		raise NamingError("unknown style %r" % style)
	key = (name, style, migration)
	hit = dic._recase_cache.get(key)
	if hit is not None:
		if isinstance(hit, NamingError):
			raise hit
		return hit
	try:
		out = _recase(name, style, dic, migration)
	except NamingError as e:
		dic._recase_cache[key] = e
		raise
	dic._recase_cache[key] = out
	return out


def _recase(name, style, dic, migration):
	prefix, raw = tokenize(name, dic)
	delimited = any(c in SEPARATORS for c in name[len(prefix):])

	if migration and len(raw) >= 2 and len(raw[0]) == 1 and raw[0].islower():
		joined = (raw[0] + raw[1]).upper()
		try:
			if all(kind != "WORD" for _, kind, _ in resolve(joined, dic, style, allow_axis=False, delimited=delimited)):
				raw = [joined] + raw[2:]
		except NamingError:
			pass

	parts = []
	for t in raw:
		parts.extend(resolve(t, dic, style, delimited=delimited))

	if style in ("CONST", "snake", "kebab", "lower"):
		flat = [c + ("s" if p else "") for c, _, p in parts]
		if style == "CONST":
			body = "_".join(s.upper() for s in flat)
		elif style == "snake":
			body = "_".join(s.lower() for s in flat)
		elif style == "kebab":
			body = "-".join(s.lower() for s in flat)
		else:
			if len(flat) > 2:
				raise NamingError("lower takes at most 2 tokens. Nest or rename %r" % name)
			body = "".join(s.lower() for s in flat)
	else:
		out = []
		for idx, (canonical, kind, plural) in enumerate(parts):
			text = _pascal_token(canonical, kind, idx == 0)
			if style == "camel" and idx == 0:
				text = _camel_first(canonical if kind != "WORD" else text, kind)
			out.append(text + ("s" if plural else ""))
		body = "".join(out)

	if body and body[0].isdigit():
		raise NamingError("%r would start with a digit in %s. Reorder the name" % (name, style))
	return prefix + body
