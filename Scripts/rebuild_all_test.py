import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

import rebuild_all
import toolchain


def write_tree(root, name, entries):
	tree = root / name
	tree.mkdir(parents=True)
	lines = [f"{key}:UNINITIALIZED={value}" for key, value in entries.items()]
	(tree / "CMakeCache.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
	return tree


class ProjectTreeTest(unittest.TestCase):

	def setUp(self):
		self.root = Path(tempfile.mkdtemp())
		self.engine = write_tree(self.root, "build", {"CMAKE_BUILD_TYPE": "Debug"})
		self.legacy = write_tree(self.root, "Legacy/Library/Compile/Debug",
								 {"Shalltear_DIR": str(self.engine)})
		self.current = write_tree(self.root, "Current/Library/Compile/Debug",
								  {"Shalltear_DIR": str(self.engine),
								   "SHALLTEAR_ENABLED_PACKAGES": "dev.shalltear.audio"})
		self.commands = []
		self.saved = (rebuild_all.REPO, rebuild_all.find_trees, rebuild_all.head_commit,
					  rebuild_all.kill_holders, rebuild_all.run, toolchain.prepare, sys.argv)
		rebuild_all.REPO = self.root
		rebuild_all.find_trees = lambda: sorted([self.engine, self.legacy, self.current])
		rebuild_all.head_commit = lambda: "0123456789ab"
		rebuild_all.kill_holders = lambda: None
		rebuild_all.run = lambda command, what: self.commands.append(command) or True
		toolchain.prepare = lambda reason: None
		sys.argv = ["rebuild_all.py"]

	def tearDown(self):
		(rebuild_all.REPO, rebuild_all.find_trees, rebuild_all.head_commit,
		 rebuild_all.kill_holders, rebuild_all.run, toolchain.prepare, sys.argv) = self.saved

	def built(self, tree):
		return any(str(tree) in command for command in self.commands)

	def test_a_tree_configured_before_the_package_set_is_skipped_and_named(self):
		out = io.StringIO()
		with contextlib.redirect_stdout(out):
			code = rebuild_all.main()
		self.assertEqual(code, 0)
		self.assertFalse(self.built(self.legacy), "the legacy tree was configured and built anyway")
		self.assertTrue(self.built(self.engine), "an engine tree after the skip went unbuilt")
		self.assertTrue(self.built(self.current), "a project tree with its set went unbuilt")
		report = out.getvalue()
		self.assertIn("skipped", report)
		self.assertIn(str(Path("Legacy/Library/Compile/Debug")), report.split("waiting on the editor")[-1])

	def test_an_empty_package_set_is_still_a_set(self):
		tree = write_tree(self.root, "Bare/Library/Compile/Debug",
						  {"Shalltear_DIR": str(self.engine), "SHALLTEAR_ENABLED_PACKAGES": ""})
		self.assertFalse(rebuild_all.lacks_package_set(tree))
		self.assertTrue(rebuild_all.lacks_package_set(self.legacy))


if __name__ == "__main__":
	unittest.main()
