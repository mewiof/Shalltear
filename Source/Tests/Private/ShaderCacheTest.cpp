#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/ShaderCompileQueue.h"

#include <filesystem>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* SURFACE = "#pragma shalltear_template unlit\n"
									"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
									"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
									"\tsurface.baseColor = tint.rgb;\n"
									"\tsurface.alpha = 1.0;\n"
									"}\n";

	constexpr const char* INCLUDING_SURFACE = "#pragma shalltear_template unlit\n"
											  "#include \"Local.hlsli\"\n"
											  "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
											  "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
											  "\tsurface.baseColor = tint.rgb * LocalScale();\n"
											  "\tsurface.alpha = 1.0;\n"
											  "}\n";

	class ShaderCacheFixture : public MaterialShadingFixture {
	protected:
		void WriteFile(const std::filesystem::path& relative, const str& content) {
			std::filesystem::create_directories((_root / relative).parent_path());
			FileSystem::WriteTextFile(Path((_root / relative).generic_string()), content);
		}

		u64 CompilesToImport(const std::filesystem::path& relative, ShaderAsset& into) {
			const u64 before = ShaderCompileQueue::GetStats().compilesElsewhere;
			EXPECT_TRUE(into.Import(Path((_root / relative).generic_string()))) << relative.generic_string();
			return ShaderCompileQueue::GetStats().compilesElsewhere - before;
		}
	};

} // anonymous namespace

TEST_F(ShaderCacheFixture, SecondImportOfUnchangedSurfaceCompilesNothing) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteFile("Surface.hlsl", SURFACE);
	ShaderAsset first;
	ASSERT_GT(CompilesToImport("Surface.hlsl", first), 0u);

	ShaderAsset second;
	EXPECT_EQ(CompilesToImport("Surface.hlsl", second), 0u);

	bool knowsCommon = false;
	for (const ShaderAsset::Dependency& dependency : second.dependencies) {
		knowsCommon = knowsCommon || dependency.physicalPath.find("ShalltearCommon.hlsli") != str::npos;
	}
	EXPECT_TRUE(knowsCommon);
}

TEST_F(ShaderCacheFixture, AnEditedIncludeRecompiles) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteFile("Local.hlsli", "float LocalScale() { return 1.0; }\n");
	WriteFile("Surface.hlsl", INCLUDING_SURFACE);
	ShaderAsset first;
	ASSERT_GT(CompilesToImport("Surface.hlsl", first), 0u);

	WriteFile("Local.hlsli", "float LocalScale() { return 0.5; }\n");
	ShaderAsset second;
	EXPECT_GT(CompilesToImport("Surface.hlsl", second), 0u);
	ASSERT_NE(first.GetVariant(SHADER_BASE_VARIANT), nullptr);
	ASSERT_NE(second.GetVariant(SHADER_BASE_VARIANT), nullptr);
	EXPECT_NE(first.GetVariant(SHADER_BASE_VARIANT)->pixelSpirv, second.GetVariant(SHADER_BASE_VARIANT)->pixelSpirv);
}

TEST_F(ShaderCacheFixture, RenamedTextCompilesOnce) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteFile("Graphs/Original.hlsl", SURFACE);
	WriteFile("Graphs/Copy.hlsl", SURFACE);
	ShaderAsset original;
	ASSERT_GT(CompilesToImport("Graphs/Original.hlsl", original), 0u);
	ShaderAsset copy;
	EXPECT_EQ(CompilesToImport("Graphs/Copy.hlsl", copy), 0u);
}

TEST_F(ShaderCacheFixture, TextInTwoDirsCompilesTwice) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteFile("A/Local.hlsli", "float LocalScale() { return 1.0; }\n");
	WriteFile("A/Surface.hlsl", INCLUDING_SURFACE);
	WriteFile("B/Local.hlsli", "float LocalScale() { return 0.25; }\n");
	WriteFile("B/Surface.hlsl", INCLUDING_SURFACE);
	ShaderAsset a;
	ASSERT_GT(CompilesToImport("A/Surface.hlsl", a), 0u);
	ShaderAsset b;
	EXPECT_GT(CompilesToImport("B/Surface.hlsl", b), 0u);
	ASSERT_NE(a.GetVariant(SHADER_BASE_VARIANT), nullptr);
	ASSERT_NE(b.GetVariant(SHADER_BASE_VARIANT), nullptr);
	EXPECT_NE(a.GetVariant(SHADER_BASE_VARIANT)->pixelSpirv, b.GetVariant(SHADER_BASE_VARIANT)->pixelSpirv);
}
