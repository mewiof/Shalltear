#!/usr/bin/env python3

import os
import shutil
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
PREBUILT_DIR = ROOT_DIR / "External" / "Prebuilt"

CXX_COMPILER = "x86_64-w64-mingw32-clang++"
C_COMPILER = "x86_64-w64-mingw32-clang"

_BIN_DIRECTORIES = (
	PREBUILT_DIR / "llvm-mingw" / "bin",
	PREBUILT_DIR / "cmake" / "bin",
	PREBUILT_DIR / "ninja",
)

FETCH_HINT = "python Scripts/fetch_prebuilt.py --toolchain"


def bin_directories():
	return [d for d in _BIN_DIRECTORIES if d.is_dir()]


def prepend_to_path(env=None):
	environment = os.environ if env is None else env
	current = environment.get("PATH", "")
	existing = [p for p in current.split(os.pathsep) if p]
	added = []
	for directory in bin_directories():
		text = str(directory)
		if text.lower() in (p.rstrip("\\/").lower() for p in existing):
			continue
		added.append(text)
	if added:
		environment["PATH"] = os.pathsep.join(added + existing)
	return added


def prepare(what="this build"):
	prepend_to_path()
	missing = [name for name in (CXX_COMPILER, "cmake", "ninja") if shutil.which(name) is None]
	if missing:
		raise SystemExit(
			f"{', '.join(missing)} not found.\n"
			f"  Fetch the pinned toolchain:  {FETCH_HINT}\n"
			f"  ...or put one on PATH. The compiler must be llvm-mingw's "
			f"GNU-target driver."
		)
	return {
		"compiler": shutil.which(CXX_COMPILER),
		"cmake": shutil.which("cmake"),
		"ninja": shutil.which("ninja"),
	}


def main():
	for directory in _BIN_DIRECTORIES:
		print(f"  {'staged ' if directory.is_dir() else 'absent '} {directory}")
	tools = prepare("this check")
	for name, path in tools.items():
		print(f"  {name:9} {path}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
