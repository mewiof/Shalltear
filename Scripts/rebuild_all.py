#!/usr/bin/env python3
"""Rebuild all CMake build trees in this checkout
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

import toolchain

REPO = Path(__file__).resolve().parent.parent

HOLDERS = ("Shalltear.exe", "ShalltearTests.exe")


def head_commit():
	try:
		out = subprocess.run(
			["git", "rev-parse", "--short=12", "HEAD"],
			cwd=REPO, capture_output=True, text=True, check=True,
		)
	except (subprocess.CalledProcessError, FileNotFoundError):
		return None
	return out.stdout.strip() or None


def kill_holders():
	for name in HOLDERS:
		subprocess.run(["taskkill", "/IM", name, "/F"],
					   capture_output=True, text=True)


def find_trees():
	trees = []
	for cache in REPO.rglob("CMakeCache.txt"):
		if "External" in cache.relative_to(REPO).parts:
			continue
		trees.append(cache.parent)
	return sorted(trees)


def config_value(config, name):
	if not config.is_file():
		return None
	marker = f'set({name} "'
	for line in config.read_text(encoding="utf-8").splitlines():
		if line.startswith(marker):
			return line[len(marker):].split('"', 1)[0]
	return None


def stamped_commit(tree):
	return config_value(tree / "CMakePackage" / "ShalltearConfig.cmake", "SHALLTEAR_GIT_COMMIT")


def cache_value(tree, name):
	prefix = f"{name}:"
	for line in (tree / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
		if line.startswith(prefix) and "=" in line:
			return line.split("=", 1)[1]
	return None


def is_project_tree(tree):
	return cache_value(tree, "Shalltear_DIR") is not None


def lacks_package_set(tree):
	return cache_value(tree, "SHALLTEAR_ENABLED_PACKAGES") is None


def reasserted_identity(tree, engine_trees):
	if not cache_value(tree, "SHALLTEAR_EXPECT_ENGINE_IDENTITY"):
		return None
	package = Path(cache_value(tree, "Shalltear_DIR")).resolve()
	if not any(package == engine or engine in package.parents for engine in engine_trees):
		return None
	config = package / "ShalltearConfig.cmake"
	version = config_value(config, "SHALLTEAR_VERSION")
	commit = config_value(config, "SHALLTEAR_GIT_COMMIT")
	if version is None or commit is None:
		return None
	return f"{version}|{commit}"


def run(command, what):
	print(f"  {what}...", flush=True)
	return subprocess.run(command, cwd=REPO).returncode == 0


def main():
	parser = argparse.ArgumentParser(description=__doc__,
									 formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("--build", action="store_true",
						help="skip the configure step. Sources are globbed WITHOUT "
							 "CONFIGURE_DEPENDS")
	args = parser.parse_args()

	toolchain.prepare("a rebuild")

	head = head_commit()
	if head is None:
		print("not a git checkout/git is not on PATH (every stamp this "
			  "script checks is a commit)")
		return 1

	trees = find_trees()
	if not trees:
		print("no build trees found. Configure one")
		return 1

	engines = [tree for tree in trees if not is_project_tree(tree)]
	trees = engines + [tree for tree in trees if is_project_tree(tree)]

	kill_holders()
	print(f"rebuilding {len(trees)} tree(s) at {head}\n")

	started = time.monotonic()
	skipped = []
	for tree in trees:
		rel = tree.relative_to(REPO)
		print("================================================================")
		print(f" {rel}")
		print("================================================================")
		if is_project_tree(tree) and lacks_package_set(tree):
			print("  skipped: configured before the editor passed its package set.")
			print("  Open the project in the editor once (it reconfigures this tree)")
			skipped.append(rel)
			continue
		configure = ["cmake", str(tree)]
		identity = reasserted_identity(tree, engines) if is_project_tree(tree) else None
		stale_claim = identity is not None and identity != cache_value(tree, "SHALLTEAR_EXPECT_ENGINE_IDENTITY")
		if identity is not None:
			print(f"  expecting engine {identity}, the package it resolves")
			configure.append(f"-DSHALLTEAR_EXPECT_ENGINE_IDENTITY={identity}")
		if (not args.build or stale_claim) and not run(configure, "configure"):
			print(f"\nFAILED to configure {rel}")
			return 1
		if not run(["cmake", "--build", str(tree), "--parallel"], "build"):
			print(f"\nFAILED to build {rel}")
			return 1

	print("\n================================================================")
	drift = [tree.relative_to(REPO) for tree in trees
			 if stamped_commit(tree) not in (None, head)]
	if drift:
		print(" BUILT, BUT STALE STAMPS:")
		for tree in drift:
			print(f"   {tree}")
		print(f" those trees did not pick up {head}. Delete and reconfigure them")
		return 1
	built = len(trees) - len(skipped)
	print(f" all {built} trees rebuilt and stamped {head} "
		  f"in {time.monotonic() - started:.0f}s")
	if skipped:
		print(f" {len(skipped)} project tree(s) skipped, waiting on the editor:")
		for tree in skipped:
			print(f"   {tree}")
	print("================================================================")
	return 0


if __name__ == "__main__":
	sys.exit(main())
