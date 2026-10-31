import argparse
import collections
import ctypes
import json
import multiprocessing
import os
import re
import sys

import naming
import naming_lint as lint

from clang import cindex

K = cindex.CursorKind

_SPELLING_LOCATION = None


def written_at(loc):
	global _SPELLING_LOCATION
	if _SPELLING_LOCATION is None:
		_SPELLING_LOCATION = cindex.conf.lib.clang_getSpellingLocation
		_SPELLING_LOCATION.argtypes = [
			cindex.SourceLocation, ctypes.POINTER(cindex.c_object_p),
			ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint),
			ctypes.POINTER(ctypes.c_uint)]
		_SPELLING_LOCATION.restype = None
	f = cindex.c_object_p()
	line, column, offset = ctypes.c_uint(), ctypes.c_uint(), ctypes.c_uint()
	_SPELLING_LOCATION(loc, ctypes.byref(f), ctypes.byref(line),
					   ctypes.byref(column), ctypes.byref(offset))
	if not f:
		return None
	return os.path.abspath(cindex.File(f).name), offset.value

REFERENCE_KINDS = {
	K.DECL_REF_EXPR,
	K.MEMBER_REF_EXPR,
	K.MEMBER_REF,
	K.TYPE_REF,
	K.TEMPLATE_REF,
	K.NAMESPACE_REF,
	K.OVERLOADED_DECL_REF,
	K.CXX_BASE_SPECIFIER,
}


def load_config():
	with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "naming-dictionary.json"), "rb") as f:
		cfg = json.loads(f.read().decode("utf-8"))
	for key in ("vendorNames", "vendorRegions", "frozenPaths", "generatedNames",
				"reservedNames", "keptNames"):
		cfg.setdefault(key, [])
	return cfg


def refuse_outside(root, path, what):
	if not naming.contains(root, path):
		sys.exit("REFUSING to %s outside the checkout\n  path: %s\n  root: %s" % (what, path, root))


class Edit:
	__slots__ = ("path", "offset", "old", "new", "kind")

	def __init__(self, path, offset, old, new, kind):
		self.path = path
		self.offset = offset
		self.old = old
		self.new = new
		self.kind = kind

	def key(self):
		return (self.path, self.offset, self.old, self.new)


def declaration_new_name(cursor, root, dic, cfg, source_root):
	name = cursor.spelling
	if not name or name.startswith(lint.SKIP_SPELLINGS):
		return None
	if not naming.IDENTIFIER.match(name):
		return None
	row = lint.row_for(cursor)
	if row is None:
		return None
	if lint.is_vendor(name, cfg["vendorNames"]) or lint.is_vendor(name, cfg["generatedNames"]) \
			or lint.is_vendor(name, cfg["reservedNames"]) or lint.is_vendor(name, cfg["keptNames"]):
		return None
	if lint.in_foreign_namespace(cursor):
		return None
	if cursor.kind in (K.CXX_METHOD, K.FUNCTION_TEMPLATE) \
			and lint.overrides_foreign(cursor, source_root):
		return None
	style = cfg["binding"]["cpp"].get(row)
	if style in (None, "matchPrimaryType"):
		return None
	try:
		want = naming.recase(name, style, dic, migration=True)
	except naming.NamingError:
		return None
	return want if want != name else None


SITE = 0
DECL = 1


def dedupe_key(tag, npath, offset, token):
	return (tag, npath, offset, token)


def record_key(rec, paths):
	return dedupe_key(rec[0], paths[rec[1]][1], rec[2], rec[3] if rec[0] == SITE else rec[4])


def scope_usr(parent, npath, root):
	if parent is None:
		return ""
	usr = parent.get_usr()
	if parent.kind != K.NAMESPACE or parent.spelling or not usr.startswith("c:"):
		return usr
	loc = parent.location
	at = lint.norm(os.path.abspath(loc.file.name)) if loc is not None and loc.file is not None else npath
	nroot = lint.norm(root)
	if naming.contains(nroot, at):
		at = at[len(nroot):].lstrip("/")
	return "c:%s%s" % (at, usr[2:])


_LOCAL_QUALIFIER = re.compile(r"^c:[^@]*@\d+@")


def one_declaration(usr):
	return _LOCAL_QUALIFIER.sub("c:", usr, count=1)


def find_collisions(scope, targets, renames):
	found = []
	for (scope_key, want), usrs in sorted(targets.items()):
		occupant = scope.get(scope_key, {}).get(want)
		if occupant is not None and occupant not in usrs and occupant not in renames:
			found.append((scope_key, want, "occupied by an existing declaration"))
		else:
			distinct = {one_declaration(u) for u in usrs}
			if len(distinct) > 1:
				found.append((scope_key, want, "%d declarations converge on it" % len(distinct)))
	return found


def collect(tu, root, dic, cfg, source_root, under_prefixes, seen):
	paths, path_ids, records = [], {}, []
	stack = list(tu.cursor.get_children())
	while stack:
		cursor = stack.pop()
		loc = cursor.location
		if loc is None or loc.file is None:
			stack.extend(cursor.get_children())
			continue
		if not lint.norm(os.path.abspath(loc.file.name)).startswith(source_root):
			continue
		stack.extend(cursor.get_children())

		written = written_at(loc)
		if written is None:
			continue
		path, offset = written
		npath = lint.norm(path)
		if not npath.startswith(source_root):
			continue
		if lint.under(path, cfg["vendorRegions"], root) or lint.under(path, cfg["frozenPaths"], root):
			continue
		refuse_outside(root, path, "record a rewrite site in a file")
		pi = path_ids.get(npath)
		if pi is None:
			pi = path_ids[npath] = len(paths)
			paths.append((path, npath))

		if cursor.kind in (K.CONSTRUCTOR, K.DESTRUCTOR):
			owner = cursor.semantic_parent
			usr = owner.get_usr() if owner is not None else None
			if usr:
				at = offset + 1 if cursor.kind == K.DESTRUCTOR else offset
				site = dedupe_key(SITE, npath, at, owner.spelling)
				if site not in seen:
					seen.add(site)
					records.append((SITE, pi, at, owner.spelling, cursor.kind.name, usr))
			continue

		if cursor.kind in REFERENCE_KINDS:
			ref = cursor.referenced
			if ref is None:
				continue
			usr = ref.get_usr()
			if not usr:
				continue
			site = dedupe_key(SITE, npath, offset, ref.spelling)
			if site not in seen:
				seen.add(site)
				records.append((SITE, pi, offset, ref.spelling, cursor.kind.name, usr))
			continue

		usr = cursor.get_usr()
		if not usr:
			continue
		key = dedupe_key(DECL, npath, offset, cursor.kind.name)
		if key in seen:
			continue
		seen.add(key)

		want = None
		if not under_prefixes or any(naming.contains(p, path) for p in under_prefixes):
			want = declaration_new_name(cursor, root, dic, cfg, source_root)
		records.append((DECL, pi, offset, cursor.spelling, cursor.kind.name, usr,
						scope_usr(cursor.semantic_parent, npath, root), want))
	return paths, records


def merge(paths, records, renames, sites, seen, stats, scope, targets):
	for rec in records:
		key = record_key(rec, paths)
		if key in seen:
			continue
		seen.add(key)
		if rec[0] == SITE:
			_, pi, offset, name, kind, usr = rec
			sites[usr].append(Edit(paths[pi][0], offset, name, None, kind))
			continue

		_, pi, offset, spelling, kind, usr, parent_usr, want = rec

		scope[parent_usr].setdefault(spelling, usr)
		if want is None:
			continue
		prev = renames.get(usr)
		if prev is not None and prev != want:
			stats["conflicts"] += 1
			sys.stderr.write("  !! %s resolves to both %s and %s\n" % (usr, prev, want))
			continue
		renames[usr] = want
		targets[(parent_usr, want)].add(usr)
		sites[usr].append(Edit(paths[pi][0], offset, spelling, None, kind))


TASKS_PER_WORKER = 6

_WORKER = {}


def worker_setup(libclang, under_prefixes):
	if os.path.exists(libclang):
		cindex.Config.set_library_file(libclang)
	root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
	_WORKER.update(root=root, dic=naming.load(), cfg=load_config(),
				   index=cindex.Index.create(), under=under_prefixes, seen=set(),
				   source_root=lint.norm(os.path.join(root, "Source")))


def parse_one(task):
	build, entry = task
	w = _WORKER
	try:
		tu = w["index"].parse(entry["file"], args=lint.compile_args(entry),
							  options=cindex.TranslationUnit.PARSE_INCOMPLETE)
	except cindex.TranslationUnitLoadError as err:
		return build, entry["file"], None, None, str(err).splitlines()[0]
	paths, records = collect(tu, w["root"], w["dic"], w["cfg"], w["source_root"],
							 w["under"], w["seen"])
	return build, entry["file"], paths, records, None


def translation_units(build, root, limit):
	db_path = os.path.join(root, build, "compile_commands.json")
	with open(db_path, "rb") as f:
		entries = json.loads(f.read().decode("utf-8"))
	source_root = lint.norm(os.path.join(root, "Source"))
	tus = [e for e in entries
		   if lint.norm(e["file"]).startswith(source_root) and e["file"].endswith(".cpp")]
	tus.sort(key=lambda e: e["file"])
	if not tus:
		sys.exit("no Source/ translation units in %s (%d entries). Wrong --build tree"
				 % (db_path, len(entries)))
	return tus[:limit] if limit else tus


def parse_trees(builds, root, jobs, libclang, under_prefixes, limit,
				renames, sites, seen, stats, scope, targets):
	tasks = []
	for build in builds:
		tus = translation_units(build, root, limit)
		sys.stderr.write("  %s: %d TUs\n" % (build, len(tus)))
		tasks.extend((build, e) for e in tus)

	def consume(results):
		for i, (build, path, paths, records, err) in enumerate(results, 1):
			if err is not None:
				stats["parseFailures"] += 1
				sys.stderr.write("  PARSE FAILED %s: %s\n" % (path, err))
			else:
				merge(paths, records, renames, sites, seen, stats, scope, targets)
			if i % 25 == 0 or i == len(tasks):
				sys.stderr.write("    %s %d/%d, %d declaration(s) to rename\n"
								 % (build, i, len(tasks), len(renames)))
				sys.stderr.flush()

	if jobs == 1:
		worker_setup(libclang, under_prefixes)
		consume(parse_one(t) for t in tasks)
		return
	ctx = multiprocessing.get_context("spawn")
	with ctx.Pool(jobs, initializer=worker_setup, initargs=(libclang, under_prefixes),
				  maxtasksperchild=TASKS_PER_WORKER) as pool:
		consume(pool.imap(parse_one, tasks))


def apply_edits(edits, stats, root, dry_run):
	by_file = collections.defaultdict(list)
	for e in edits:
		by_file[e.path].append(e)

	changed, covered = [], set()
	for path, items in sorted(by_file.items()):
		refuse_outside(root, path, "write a file")
		with open(path, "rb") as f:
			data = f.read()
		items.sort(key=lambda e: e.offset, reverse=True)
		out = data
		applied = 0
		last_offset = None
		for e in items:
			old = e.old.encode("utf-8")
			new = e.new.encode("utf-8")
			if last_offset is not None and e.offset + len(old) > last_offset:
				stats["overlapping"] += 1
				sys.stderr.write("  !! overlapping edit in %s at %d\n" % (path, e.offset))
				continue
			if out[e.offset:e.offset + len(old)] != old:
				stats["textMismatch"] += 1
				stats["mismatchExamples"].append(
					(path, e.offset, e.old, out[e.offset:e.offset + len(old)].decode("utf-8", "replace")))
				continue
			out = out[:e.offset] + new + out[e.offset + len(old):]
			last_offset = e.offset
			applied += 1
			covered.add((lint.norm(path), e.offset))
		if not applied:
			continue
		stats["edits"] += applied
		changed.append((path, applied))
		if not dry_run:
			with open(path, "wb") as f:
				f.write(out)
	return changed, covered


def classify(text):
	out = bytearray(len(text))
	i, n = 0, len(text)
	define_depth = 0
	at_line_start = True
	while i < n:
		c = text[i]
		if c == "\n":
			if define_depth:
				define_depth = define_depth - 1 if not _continues(text, i) else define_depth
			at_line_start = True
			i += 1
			continue
		if at_line_start and c in " \t":
			out[i] = 3 if define_depth else 0
			i += 1
			continue
		if at_line_start and text.startswith("#define", i):
			define_depth = 1
		at_line_start = False
		two = text[i:i + 2]
		if two == "//":
			j = text.find("\n", i)
			j = n if j < 0 else j
			for k in range(i, j):
				out[k] = 1
			i = j
			continue
		if two == "/*":
			j = text.find("*/", i + 2)
			j = n if j < 0 else j + 2
			for k in range(i, j):
				out[k] = 1
			i = j
			continue
		if c in "\"'":
			j = i + 1
			while j < n and text[j] != c:
				j += 2 if text[j] == "\\" else 1
			j = min(j + 1, n)
			for k in range(i, j):
				out[k] = 2
			i = j
			continue
		out[i] = 3 if define_depth else 0
		i += 1
	return out


def _continues(text, newline_index):
	j = newline_index - 1
	while j >= 0 and text[j] in " \t\r":
		j -= 1
	return j >= 0 and text[j] == "\\"


def residual_occurrences(root, old_names, covered):
	if not old_names:
		return []
	pattern = re.compile(r"\b(" + "|".join(re.escape(n) for n in sorted(old_names)) + r")\b")
	kinds = ("code", "comment", "string", "#define body")
	found = []
	for dirpath, dirnames, filenames in os.walk(os.path.join(root, "Source")):
		for fn in sorted(filenames):
			if not fn.endswith((".h", ".hpp", ".cpp", ".inl", ".hlsl")):
				continue
			path = os.path.join(dirpath, fn)
			with open(path, "rb") as f:
				raw = f.read()
			try:
				text = raw.decode("utf-8")
			except UnicodeDecodeError:
				continue
			if not pattern.search(text):
				continue
			ascii_only = raw.isascii()
			context = classify(text)
			rel = os.path.relpath(path, root).replace(os.sep, "/")
			for m in pattern.finditer(text):
				start = m.start()
				byte_offset = start if ascii_only else len(text[:start].encode("utf-8"))
				if (lint.norm(path), byte_offset) in covered:
					continue
				found.append((m.group(1), rel, text.count("\n", 0, start) + 1,
							  kinds[context[start]]))
	return found


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--build", action="append", default=[],
					help="a build tree to read compile_commands.json from. Pass BOTH link modes")
	ap.add_argument("--under", action="append", default=[],
					help="only rename declarations under this repo-relative path or file. Repeatable "
						 "(references follow anywhere)")
	ap.add_argument("--apply", action="store_true", help="write the files (default is a dry run)")
	ap.add_argument("--limit", type=int, default=0)
	ap.add_argument("--jobs", type=int, default=0,
					help="parse processes. 0 picks 1/core, 1 stays in this process. Each "
						 "costs libclang's retention (see TASKS_PER_WORKER) -> lower it on a "
						 "machine with less memory than cores * 1/2 GB")
	ap.add_argument("--libclang", default=lint.LIBCLANG)
	args = ap.parse_args()

	builds = args.build or ["build", "build-static"]
	root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
	jobs = args.jobs or (os.cpu_count() or 1)
	cfg = load_config()

	under_prefixes = []
	for u in args.under:
		scope_path = os.path.abspath(os.path.join(root, u))
		refuse_outside(root, scope_path, "scope the campaign to a path")
		under_prefixes.append(scope_path)

	renames = {}
	sites = collections.defaultdict(list)
	seen = set()
	scope = collections.defaultdict(dict)
	targets = collections.defaultdict(set)
	stats = collections.Counter()
	stats["mismatchExamples"] = []

	parse_trees(builds, root, jobs, args.libclang, under_prefixes, args.limit,
				renames, sites, seen, stats, scope, targets)

	collisions = find_collisions(scope, targets, renames)
	if collisions:
		stats["collisions"] = len(collisions)

	edits, deduped = [], set()
	for usr, new in renames.items():
		for site in sites[usr]:
			if site.old != new:
				site.new = new
				k = site.key()
				if k not in deduped:
					deduped.add(k)
					edits.append(site)

	print("\n==== CODEMOD ====")
	print("  build trees              : %s" % ", ".join(builds))
	print("  parse processes          : %d" % jobs)
	print("  scope                    : %s" % (", ".join(args.under) or "the whole tree"))
	print("  declarations to rename   : %d" % len(renames))
	print("  spellings to rewrite     : %d  (declarations and references)" % len(edits))
	if stats["parseFailures"]:
		print("  TU parse failures        : %d" % stats["parseFailures"])
	if stats["conflicts"]:
		print("  !! conflicting targets   : %d" % stats["conflicts"])
	if collisions:
		print("  !! NAME COLLISIONS       : %d. Review before applying" % len(collisions))
		for scope_key, want, why in collisions[:20]:
			print("      %-28s in %s: %s" % (want, scope_key or "<file scope>", why))

	renames_by_old = {e.old: e.new for e in edits}
	changed, covered = apply_edits(edits, stats, root, dry_run=not args.apply)
	print("\n  files %s : %d" % ("changed" if args.apply else "that would change", len(changed)))
	print("  spellings applied        : %d" % stats["edits"])
	if stats["textMismatch"]:
		print("  skipped, text not there  : %d  (macro expansions, typically)" % stats["textMismatch"])
		for path, offset, want, got in stats["mismatchExamples"][:10]:
			print("      %s@%d wanted %r, found %r" % (os.path.relpath(path, root), offset, want, got))
	if stats["overlapping"]:
		print("  skipped, overlapping     : %d" % stats["overlapping"])

	residual = residual_occurrences(root, set(renames_by_old), covered)
	in_code = [r for r in residual if r[3] in ("code", "#define body")]
	if residual:
		print("\n==== STILL SPELLED THE OLD WAY (no edit covers these) ====")
		print("    %d in code, %d in comments and strings. A CODE residue doesn't"
			  % (len(in_code), len(residual) - len(in_code)))
		print("    compile once the declaration moves -> the hand pass belongs in")
		print("    THIS commit. The rest is a judgement about what the text means")
		by_name = collections.defaultdict(list)
		for name, rel, line, kind in residual:
			by_name[name].append((rel, line, kind))
		for name, places in sorted(by_name.items(), key=lambda kv: (-len(kv[1]), kv[0])):
			code = sum(1 for p in places if p[2] in ("code", "#define body"))
			print("  %-24s -> %-24s %3d left (%d in code)"
				  % (name, renames_by_old.get(name, "?"), len(places), code))
			for rel, line, kind in places[:8]:
				print("      %-13s %s:%d" % (kind, rel, line))
			if len(places) > 8:
				print("      ... %d more" % (len(places) - 8))

	by_change = collections.Counter("%s -> %s" % (e.old, e.new) for e in edits)
	print("\n==== REWRITES (%d distinct) ====" % len(by_change))
	for change, n in by_change.most_common():
		print("  %5d  %s" % (n, change))

	if not args.apply:
		print("\ndry run, nothing written. Re-run with --apply")
	return 1 if (stats["conflicts"] or stats["overlapping"] or collisions or residual) else 0


if __name__ == "__main__":
	sys.exit(main())
