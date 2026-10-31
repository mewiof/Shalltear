import unittest

import build_sdk


class ArtifactDirtTest(unittest.TestCase):
	def test_every_tracked_change_counts(self):
		porcelain = "\n".join((
			" M Source/Core/Private/Scene.cpp",
			"M  README.md",
			"D  Docs/Old.md",
			"R  Notes.md -> Docs/Notes.md",
			"A  .github/workflows/new.yml",
		))
		self.assertEqual(build_sdk.artifact_dirt(porcelain), porcelain.splitlines())

	def test_an_untracked_path_below_the_root_counts(self):
		porcelain = "\n".join((
			"?? Source/Core/Private/Forgotten.cpp",
			"?? Samples/NewSample/",
			"?? EngineAssets/Shaders/New.hlsl",
			"?? Plugins/",
			'?? "Source/Core/Private/With Space.cpp"',
		))
		self.assertEqual(build_sdk.artifact_dirt(porcelain), porcelain.splitlines())

	def test_a_loose_root_file_or_a_root_dot_directory_does_not(self):
		porcelain = "\n".join((
			"?? NOTES.md",
			"?? CMakeUserPresets.json",
			"?? .scratch/",
			"?? .vscode/launch.json",
			"?? .clangd",
		))
		self.assertEqual(build_sdk.artifact_dirt(porcelain), [])

	def test_the_two_kinds_mixed_keep_only_what_reaches_an_artifact(self):
		porcelain = "?? NOTES.md\n M Source/Core/Private/Scene.cpp\n?? .scratch/\n?? Source/New.cpp\n\n"
		self.assertEqual(build_sdk.artifact_dirt(porcelain), [
			" M Source/Core/Private/Scene.cpp",
			"?? Source/New.cpp",
		])

	def test_a_clean_tree_has_none(self):
		self.assertEqual(build_sdk.artifact_dirt(""), [])


if __name__ == "__main__":
	unittest.main()
