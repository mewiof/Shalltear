#include "TestsPCH.h"

#include "PESubsystem.h"

#include "Shalltear/Editor/ScriptProjectOps.h"

#include <filesystem>

using namespace Shalltear;

TEST(ArtifactSubsystemTest, TheEditorExecutableIsAWindowedApplication) {
	std::filesystem::path here(ScriptProjectOps::GetEditorExecutablePath());
	if (here.empty() || !here.has_parent_path()) {
		GTEST_SKIP();
	}
	std::filesystem::path editor = here.parent_path() / "Shalltear.exe";
	if (!std::filesystem::exists(editor)) {
		GTEST_SKIP() << "no editor exe (" << editor.string() << ")";
	}

	EXPECT_EQ(PESubsystem::Read(here), PESubsystem::CONSOLE);
	EXPECT_EQ(PESubsystem::Read(editor), PESubsystem::GUI);
}
