#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Serialization/JSON.h"

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	class GeneratingImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("ShaderAsset"); }

		Asset* Import(const Path& sourcePath) override { return ImportWithSettings(sourcePath, Path(), JSONValue{}); }

		Asset* ImportWithSettings(const Path& sourcePath, const Path&, const JSONValue&) override {
			++imports;
			str color = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
			str generated = "#pragma shalltear_template unlit\n"
							"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
							"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
							"\tsurface.baseColor = float3(" +
							color +
							");\n"
							"\tsurface.alpha = 1.0;\n"
							"}\n";
			Path generatedPath = FileSystem::Resolve(Path("project://Library/Generated/Tint.hlsl"));
			FileSystem::CreateDirectory(FileSystem::Resolve(Path("project://Library/Generated")));
			FileSystem::WriteTextFile(generatedPath, generated);
			ShaderAsset* shader = New<ShaderAsset>(MemTag::Asset);
			if (!shader->Import(generatedPath)) {
				Delete(shader);
				return nullptr;
			}
			return shader;
		}

		u32 imports = 0;
	};

	class ShaderReloadFixture : public MaterialReadbackFixture {
	protected:
		void SetUp() override {
			MaterialReadbackFixture::SetUp();
			AssetImporterRegistry::RegisterExtension(".probeshader", &_importer);
		}

		void TearDown() override {
			AssetImporterRegistry::Unregister(&_importer);
			MaterialReadbackFixture::TearDown();
		}

		Vec3 Drawn(const ShadingStage& stage) {
			Vector<u8> pixels;
			Vec2I size{};
			if (!RenderStage(stage, pixels, size) || pixels.empty()) {
				ADD_FAILURE() << "frame readback failed";
				return {};
			}
			return CenterTexel(pixels, size);
		}

		GeneratingImporter _importer;
	};

} // anonymous namespace

TEST_F(ShaderReloadFixture, EditToGeneratedShadersSourceRegeneratesIt) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Tint.probeshader", "1, 0, 0");
	Asset* shaderAsset = AssetDatabase::LoadAsset(Path("project://Tint.probeshader"));
	ASSERT_NE(shaderAsset, nullptr);
	MaterialAsset* material = LoadMaterial("Tint.mat", shaderAsset->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	Vec3 before = Drawn(stage);
	ASSERT_GT(before.x, 200.0f);
	ASSERT_LT(before.y, 20.0f);
	const u32 importedOnce = _importer.imports;

	WriteProjectFile("Tint.probeshader", "0, 1, 0");
	RenderSystem::NotifyShaderFileChanged((_root / "Tint.probeshader").generic_string().c_str());
	Vec3 after = Drawn(stage);
	EXPECT_EQ(_importer.imports, importedOnce + 1);
	EXPECT_GT(after.y, 200.0f);
	EXPECT_LT(after.x, 20.0f);

	TearDownStage(stage);
}

TEST_F(ShaderReloadFixture, DerivedDataAndUnrelatedFilesReloadNothing) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Tint.probeshader", "1, 0, 0");
	Asset* shaderAsset = AssetDatabase::LoadAsset(Path("project://Tint.probeshader"));
	ASSERT_NE(shaderAsset, nullptr);
	MaterialAsset* material = LoadMaterial("Tint.mat", shaderAsset->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	ASSERT_GT(Drawn(stage).x, 200.0f);
	const u32 importedOnce = _importer.imports;

	auto* shader = static_cast<ShaderAsset*>(shaderAsset);
	ASSERT_FALSE(shader->dependencies.empty());
	str generated = shader->dependencies[0].physicalPath;
	ASSERT_NE(generated.find("Library"), str::npos);
	RenderSystem::NotifyShaderFileChanged(generated);
	Drawn(stage);
	EXPECT_EQ(_importer.imports, importedOnce);

	WriteProjectFile("Unrelated.txt", "nothing");
	RenderSystem::NotifyShaderFileChanged((_root / "Unrelated.txt").generic_string().c_str());
	Drawn(stage);
	EXPECT_EQ(_importer.imports, importedOnce);

	RenderSystem::NotifyShaderFileChanged((_root / "Tint.probeshader").generic_string().c_str());
	Drawn(stage);
	EXPECT_EQ(_importer.imports, importedOnce + 1);

	TearDownStage(stage);
}

TEST_F(ShaderReloadFixture, ShaderReloadsOnce) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Tint.probeshader", "1, 0, 0");
	Asset* shaderAsset = AssetDatabase::LoadAsset(Path("project://Tint.probeshader"));
	ASSERT_NE(shaderAsset, nullptr);
	MaterialAsset* material = LoadMaterial("Tint.mat", shaderAsset->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	ASSERT_GT(Drawn(stage).x, 200.0f);
	const u32 importedOnce = _importer.imports;

	WriteProjectFile("Tint.probeshader", "0, 1, 0");
	str source = (_root / "Tint.probeshader").generic_string();
	str backslashed = source;
	std::replace(backslashed.begin(), backslashed.end(), '/', '\\');
	RenderSystem::NotifyShaderFileChanged(source);
	RenderSystem::NotifyShaderFileChanged(backslashed);
	EXPECT_GT(Drawn(stage).y, 200.0f);
	EXPECT_EQ(_importer.imports, importedOnce + 1);

	TearDownStage(stage);
}

TEST_F(ShaderReloadFixture, JointChangeReloadsOnce) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const char* surface = "#pragma shalltear_template unlit\n"
						  "#include \"TintColor.hlsli\"\n"
						  "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
						  "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
						  "\tsurface.baseColor = TINT;\n"
						  "\tsurface.alpha = 1.0;\n"
						  "}\n";
	WriteProjectFile("TintColor.hlsli", "#define TINT float3(1, 0, 0)\n");
	WriteProjectFile("Tint.hlsl", surface);
	Asset* shaderAsset = AssetDatabase::LoadAsset(Path("project://Tint.hlsl"));
	ASSERT_NE(shaderAsset, nullptr);
	auto* shader = static_cast<ShaderAsset*>(shaderAsset);
	MaterialAsset* material = LoadMaterial("Tint.mat", shaderAsset->assetId, {});
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	ASSERT_GT(Drawn(stage).x, 200.0f);
	const u32 versionBefore = shader->version;

	WriteProjectFile("TintColor.hlsli", "#define TINT float3(0, 1, 0)\n");
	WriteProjectFile("Tint.hlsl", str(surface) + "// moved by the same checkout\n");
	RenderSystem::NotifyShaderFileChanged((_root / "Tint.hlsl").generic_string());
	RenderSystem::NotifyShaderFileChanged((_root / "TintColor.hlsli").generic_string());
	EXPECT_GT(Drawn(stage).y, 200.0f);
	EXPECT_EQ(shader->version, versionBefore + 1);

	TearDownStage(stage);
}
