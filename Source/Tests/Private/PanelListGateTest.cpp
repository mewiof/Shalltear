#include "TestsPCH.h"

#include "Checkout.h"
#include "UITestUtil.h"

#include <filesystem>
#include <fstream>

using namespace Shalltear;

namespace {
	namespace fs = std::filesystem;
}

namespace {

	struct AssemblyToken {
		const char* token;
		const char* why;
	};

	const AssemblyToken assemblyTokens[] = {
		{"GetPressMods", "press mods live in EditorListView::Click"},
		{"SetTreeSelection", "widget restyles both layouts"},
		{"ScrollIntoView", "widget follows the cursor"},
		{"KeyMod", "modifiers are selection gestures"},
		{"Key::Down", "arrows are TranslateKey's"},
		{"Key::Up", "arrows are TranslateKey's"},
		{"Key::Home", "Home/End are TranslateKey's"},
		{"Key::End", "Home/End are TranslateKey's"},
		{"Key::PageUp", "paging is PageStep's"},
		{"Key::PageDown", "paging is PageStep's"},
	};

	struct PanelException {
		const char* path;
		const char* why;
	};

	const PanelException panelExceptions[] = {
		{"Source/Editor/Private/Panels/PanelHeader.h",
		 "header search field (text field Escape)"},
		{"Source/Editor/Private/Panels/ConsolePanel.cpp",
		 "expansion list on EditorVirtualRows"},
		{"Source/Editor/Private/Panels/SceneViewPanel.cpp",
		 "viewport (no rows, no selection)"},
		{"Source/Editor/Private/Panels/RuntimeViewPanel.cpp",
		 "game viewport, declines every key"},
	};

	str CodeOnly(const str& line) {
		size_t comment = line.find("//");
		return comment == str::npos ? line : line.substr(0, comment);
	}

	bool FileSpells(const fs::path& file, const char* token) {
		std::ifstream stream(file);
		str line;
		while (std::getline(stream, line)) {
			if (CodeOnly(line).find(token) != str::npos) {
				return true;
			}
		}
		return false;
	}

} // namespace

TEST(PanelListSourceGate, NoPanelHandAssemblesListAnyMore) {
	fs::path root = Checkout::Root();
	ASSERT_FALSE(root.empty()) << fs::current_path().string();
	fs::path panels = root / "Source/Editor/Private/Panels";
	ASSERT_TRUE(fs::exists(panels)) << root.string();

	Vector<str> offenders{GetMemoryResource(MemTag::Core)};
	u32 scanned = 0;
	std::error_code ec;
	for (const auto& entry : fs::recursive_directory_iterator(panels, ec)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		str extension = entry.path().extension().string();
		if (extension != ".cpp" && extension != ".h") {
			continue;
		}
		++scanned;
		str relative = fs::relative(entry.path(), root, ec).generic_string();
		for (const AssemblyToken& token : assemblyTokens) {
			if (FileSpells(entry.path(), token.token)) {
				offenders.push_back(relative + " spells " + token.token + " (" + token.why + ")");
			}
		}
	}
	EXPECT_GT(scanned, 20u);

	for (const str& offender : offenders) {
		bool allowed = false;
		for (const PanelException& exception : panelExceptions) {
			allowed = allowed || offender.rfind(exception.path, 0) == 0;
		}
		EXPECT_TRUE(allowed);
	}
}

TEST(PanelListSourceGate, ExceptionsStillNeeded) {
	fs::path root = Checkout::Root();
	ASSERT_FALSE(root.empty());

	for (const PanelException& exception : panelExceptions) {
		fs::path file = root / exception.path;
		ASSERT_TRUE(fs::exists(file)) << exception.path;
		bool spells = false;
		for (const AssemblyToken& token : assemblyTokens) {
			spells = spells || FileSpells(file, token.token);
		}
		EXPECT_TRUE(spells);
	}
}

TEST(PanelListSourceGate, TheScanCanSayYesAndNo) {
	fs::path root = Checkout::Root();
	ASSERT_FALSE(root.empty());

	EXPECT_TRUE(FileSpells(root / "Source/Editor/Private/Panels/SceneViewPanel.cpp", "KeyMod"));
	EXPECT_FALSE(FileSpells(root / "Source/Editor/Private/Panels/HierarchyPanel.cpp", "KeyMod"));
	EXPECT_FALSE(FileSpells(root / "Source/Editor/Private/Panels/HierarchyPanel.h", "SetTreeSelection"));
}
