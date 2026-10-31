#include <gtest/gtest.h>

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "Asset/EngineBuiltins.h"

#include <algorithm>
#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	struct BuiltinExpectation {
		const char* engineRelativePath;
		Guid expected;
	};

	constexpr BuiltinExpectation Expectations[] = {
		{"Shaders/PresentPass.hlsl", EngineBuiltins::PresentPassShader},
		{"Shaders/Lit.hlsl", EngineBuiltins::LitShader},
		{"Shaders/DebugLines.hlsl", EngineBuiltins::DebugLinesShader},
		{"Shaders/Wireframe.hlsl", EngineBuiltins::WireframeShader},
		{"Shaders/Sky.hlsl", EngineBuiltins::SkyShader},
		{"Shaders/UI.hlsl", EngineBuiltins::UIShader},
		{"Shaders/Blit.hlsl", EngineBuiltins::BlitShader},
		{"Shaders/DebugView.hlsl", EngineBuiltins::DebugViewShader},
		{"Shaders/DebugOverlay.hlsl", EngineBuiltins::DebugOverlayShader},
		{"Fonts/Inter/Inter-Regular.ttf", EngineBuiltins::DefaultFont},
		{"Shaders/DepthCopy.hlsl", EngineBuiltins::DepthCopyShader},
		{"Shaders/Outline.hlsl", EngineBuiltins::OutlineShader},
		{"Shaders/SkyPrefilter.hlsl", EngineBuiltins::SkyPrefilterShader},
	};

	class EngineBuiltinsFixture : public ::testing::Test {
	protected:
		std::filesystem::path _engineAssets;

		void SetUp() override {
			_engineAssets = FindRepoDir("EngineAssets");
			if (_engineAssets.empty()) {
				GTEST_SKIP() << "EngineAssets not found";
			}
		}

		Guid ReadSidecarGuid(const char* engineRelativePath) {
			std::filesystem::path meta = _engineAssets / (str(engineRelativePath) + ".meta");
			str text = FileSystem::ReadTextFile(Path(meta.generic_string()), MemTag::Asset);
			if (text.empty()) {
				return {};
			}
			LinearAllocator arena(text.size() + 64, MemTag::Asset);
			JSONValue root = JSONParse(text, arena);
			return Guid::FromString(root["assetId"].AsString());
		}
	};

} // namespace

TEST_F(EngineBuiltinsFixture, EveryConstantMatchesItsCommittedSidecar) {
	for (const BuiltinExpectation& row : Expectations) {
		Guid onDisk = ReadSidecarGuid(row.engineRelativePath);
		ASSERT_TRUE(onDisk.IsValid()) << row.engineRelativePath;
		EXPECT_EQ(onDisk, row.expected);
	}
}

TEST_F(EngineBuiltinsFixture, EveryBuiltinSourceFileExists) {
	for (const BuiltinExpectation& row : Expectations) {
		std::filesystem::path source = _engineAssets / row.engineRelativePath;
		EXPECT_TRUE(std::filesystem::exists(source)) << "EngineAssets/" << row.engineRelativePath;
	}
}

TEST_F(EngineBuiltinsFixture, RootsHoldsEveryBuiltinAndNothingElse) {
	std::span<const Guid> roots = AssetDatabase::GetEngineBuiltinRoots();
	ASSERT_EQ(roots.size(), std::size(Expectations));

	for (const BuiltinExpectation& row : Expectations) {
		EXPECT_NE(std::find(roots.begin(), roots.end(), row.expected), roots.end());
	}
}

TEST_F(EngineBuiltinsFixture, RootsAreDistinctAndValid) {
	std::span<const Guid> roots = AssetDatabase::GetEngineBuiltinRoots();
	for (size_t i = 0; i < roots.size(); ++i) {
		EXPECT_TRUE(roots[i].IsValid()) << "root " << i;
		for (size_t j = i + 1; j < roots.size(); ++j) {
			EXPECT_NE(roots[i], roots[j]) << "roots " << i << " and " << j;
		}
	}
}
