#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class AssetCreationFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_create_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);

			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}
	};

} // namespace

TEST_F(AssetCreationFixture, CreatableImportersAdvertiseThemselves) {
	IAssetImporter* material = AssetImporterRegistry::Find(StringHash("MaterialAsset"));
	ASSERT_NE(material, nullptr);
	EXPECT_TRUE(material->CanCreateAsset());
	EXPECT_EQ(material->GetCreateMenuLabel(), "Material");
	EXPECT_EQ(material->GetCreateDefaultFileName(), "New Material.mat");

	IAssetImporter* mesh = AssetImporterRegistry::Find(StringHash("MeshAsset"));
	ASSERT_NE(mesh, nullptr);
	EXPECT_FALSE(mesh->CanCreateAsset());
	EXPECT_TRUE(mesh->GetCreateMenuLabel().empty());
}

TEST_F(AssetCreationFixture, ForEachEnumeratesRegistrationsWithPrimaryExtensions) {
	std::unordered_map<u64, str> extensionByType;
	u32 creatableCount = 0;
	AssetImporterRegistry::ForEach([&](StringHash typeId, IAssetImporter* importer, std::string_view primaryExtension) {
		extensionByType[typeId.GetValue()] = str(primaryExtension);
		if (importer && importer->CanCreateAsset()) {
			++creatableCount;
		}
	});

	EXPECT_EQ(extensionByType[StringHash("MaterialAsset").GetValue()], ".mat");
	EXPECT_EQ(extensionByType[StringHash("TextAsset").GetValue()], ".txt");
	EXPECT_EQ(extensionByType[StringHash("TextureAsset").GetValue()], ".hdr");
	EXPECT_GE(creatableCount, 2u);
}

TEST_F(AssetCreationFixture, CreateWritesSourceMetaIndex) {
	Guid guid = AssetDatabase::CreateAsset(Path("project://Materials/Fresh.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(guid.IsValid());

	EXPECT_TRUE(FileSystem::Exists(Path("project://Materials/Fresh.mat")));
	EXPECT_TRUE(FileSystem::Exists(Path("project://Materials/Fresh.mat.meta")));
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Materials/Fresh.mat")), guid);
	EXPECT_EQ(AssetDatabase::LookupPath(guid).ToString(), "project://Materials/Fresh.mat");
}

TEST_F(AssetCreationFixture, CreateAssetCreatesMissingParentDirectories) {
	Guid guid = AssetDatabase::CreateAsset(Path("project://A/B/C/Deep.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(guid.IsValid());
	EXPECT_TRUE(std::filesystem::exists(_root / "A" / "B" / "C" / "Deep.mat"));
}

TEST_F(AssetCreationFixture, CreateAssetIsNotLeftLoaded) {
	Guid guid = AssetDatabase::CreateAsset(Path("project://Once.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(guid.IsValid());

	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->GetRefCount(), 1);
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(AssetCreationFixture, CreatedSceneReloadsAsAnEmptyScene) {
	Guid guid = AssetDatabase::CreateAsset(Path("project://Empty.scene"), StringHash("SceneAsset"));
	ASSERT_TRUE(guid.IsValid());

	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->GetAssetTypeId(), StringHash("SceneAsset"));
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(AssetCreationFixture, CreatedSceneKeepsBrowserName) {
	ASSERT_TRUE(AssetDatabase::CreateAsset(Path("project://Scenes/New Scene.scene"), StringHash("SceneAsset")).IsValid());

	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://Scenes/New Scene.scene"));
	ASSERT_TRUE(scene.IsValid());
	EXPECT_EQ(scene.Get()->name, "New Scene");
}

TEST_F(AssetCreationFixture, CreateAssetRefusesAnExistingPath) {
	ASSERT_TRUE(AssetDatabase::CreateAsset(Path("project://Taken.mat"), StringHash("MaterialAsset")).IsValid());

	Guid second = AssetDatabase::CreateAsset(Path("project://Taken.mat"), StringHash("MaterialAsset"));
	EXPECT_FALSE(second.IsValid());
}

TEST_F(AssetCreationFixture, CreateRefusesUncreatableTypes) {
	EXPECT_FALSE(AssetDatabase::CreateAsset(Path("project://x.gltf"), StringHash("MeshAsset")).IsValid());
	EXPECT_FALSE(AssetDatabase::CreateAsset(Path("project://x.bogus"), StringHash("NoSuchAsset")).IsValid());

	EXPECT_FALSE(FileSystem::Exists(Path("project://x.gltf")));
	EXPECT_FALSE(FileSystem::Exists(Path("project://x.bogus")));
}

namespace {

	std::filesystem::path EngineAssetsDir() {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / "EngineAssets";
			if (std::filesystem::is_directory(candidate / "Shaders", ec)) {
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

	class ShaderCreationFixture : public AssetCreationFixture {
	protected:
		bool _mountedEngine = false;

		void SetUp() override {
			AssetCreationFixture::SetUp();
			std::filesystem::path engineAssets = EngineAssetsDir();
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
		}

		void TearDown() override {
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			AssetCreationFixture::TearDown();
		}
	};

} // namespace

TEST_F(ShaderCreationFixture, NewMaterialLoadsShaderDefaults) {
	ASSERT_TRUE(_mountedEngine);

	Guid guid = AssetDatabase::CreateAsset(Path("project://Default.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(guid.IsValid());

	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->GetAssetTypeId(), StringHash("MaterialAsset"));

	MaterialAsset* material = static_cast<MaterialAsset*>(loaded);
	if (material->params.empty()) {
		AssetDatabase::UnloadAsset(loaded);
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	EXPECT_EQ(material->GetColor("baseColor"), Color::White());
	EXPECT_FLOAT_EQ(material->GetFloat("metallic", -1.0f), 0.0f);
	EXPECT_FLOAT_EQ(material->GetFloat("roughness", -1.0f), 0.5f);
	EXPECT_FLOAT_EQ(material->GetFloat("reflectance", -1.0f), 0.5f);
	EXPECT_FLOAT_EQ(material->GetFloat("normalScale", -1.0f), 1.0f);
	EXPECT_FLOAT_EQ(material->GetFloat("occlusionStrength", -1.0f), 1.0f);
	EXPECT_FALSE(material->HasFloat(StringHash("specular")));

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(ShaderCreationFixture, ShaderImporterCreatesThroughSourceHook) {
	IAssetImporter* shader = AssetImporterRegistry::Find(StringHash("ShaderAsset"));
	ASSERT_NE(shader, nullptr);
	EXPECT_TRUE(shader->CanCreateAsset());
	EXPECT_EQ(shader->GetCreateMenuLabel(), "Shader");
	EXPECT_EQ(shader->GetCreateDefaultFileName(), "New Shader.hlsl");

	EXPECT_EQ(shader->CreateAsset(), nullptr);
	Buffer source;
	ASSERT_TRUE(shader->CreateAssetSource(source));
	ASSERT_TRUE(source);
	EXPECT_GT(source.size, 0u);

	IAssetImporter* material = AssetImporterRegistry::Find(StringHash("MaterialAsset"));
	ASSERT_NE(material, nullptr);
	Buffer none;
	EXPECT_FALSE(material->CreateAssetSource(none));
}

TEST_F(ShaderCreationFixture, CreatingShaderWritesTemplateThatCompiles) {
	ASSERT_TRUE(_mountedEngine);

	Guid guid = AssetDatabase::CreateAsset(Path("project://New Shader.hlsl"), StringHash("ShaderAsset"));
	ASSERT_TRUE(guid.IsValid());
	EXPECT_TRUE(std::filesystem::exists(_root / "New Shader.hlsl"));

	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(loaded);
	if (shader->variants.empty()) {
		AssetDatabase::UnloadAsset(loaded);
		GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
	}
	EXPECT_FALSE(shader->variants[SHADER_BASE_VARIANT].vertexSpirv.empty());
	EXPECT_FALSE(shader->variants[SHADER_BASE_VARIANT].pixelSpirv.empty());
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(ShaderCreationFixture, TheTemplateKeepsAFreshMaterialsParameters) {
	ASSERT_TRUE(_mountedEngine);

	Guid guid = AssetDatabase::CreateAsset(Path("project://New Shader.hlsl"), StringHash("ShaderAsset"));
	ASSERT_TRUE(guid.IsValid());
	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);
	ShaderAsset* shader = static_cast<ShaderAsset*>(loaded);
	const UniformBlockLayout* layout = shader->GetMaterialParamLayout();
	if (!layout) {
		AssetDatabase::UnloadAsset(loaded);
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}

	bool baseColor = false;
	bool metallic = false;
	bool roughness = false;
	for (const UniformMember& member : layout->members) {
		baseColor = baseColor || member.name == "baseColor";
		metallic = metallic || member.name == "metallic";
		roughness = roughness || member.name == "roughness";
	}
	EXPECT_TRUE(baseColor);
	EXPECT_TRUE(metallic);
	EXPECT_TRUE(roughness);

	EXPECT_EQ(shader->integrator, ShaderTemplate::Lit);

	Vector<str> textures{GetMemoryResource(MemTag::Asset)};
	shader->GetMaterialTextureSlots(textures);
	ASSERT_EQ(textures.size(), 1u);
	EXPECT_EQ(textures[0], "baseColorTexture");
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(AssetCreationFixture, MeshAssignBindsImportedMaterials) {
	UndoSystem::Initialize();

	Guid first = AssetDatabase::CreateAsset(Path("project://First.mat"), StringHash("MaterialAsset"));
	Guid second = AssetDatabase::CreateAsset(Path("project://Second.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());

	MeshAsset mesh;
	SubMesh a;
	a.firstIndex = 0;
	a.indexCount = 3;
	a.materialSlot = 0;
	SubMesh b;
	b.firstIndex = 3;
	b.indexCount = 3;
	b.materialSlot = 1;
	mesh.submeshes.push_back(a);
	mesh.submeshes.push_back(b);
	mesh.defaultMaterials.push_back(first);
	mesh.defaultMaterials.push_back(second);

	Handle<Scene> scene = SceneSystem::CreateScene("Bind");
	Handle<SceneObject> object = SceneSystem::CreateObject("R", scene);
	Handle<MeshRenderer> renderer = object.Get()->AddComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	Handle<Object> target(renderer.Get());

	EXPECT_EQ(EditorSceneOps::BindDefaultMaterials(target, mesh.GetInstanceId()), 2u);
	ASSERT_EQ(renderer.Get()->GetMaterialCount(), 2u);
	EXPECT_TRUE(renderer.Get()->GetMaterial(0).IsValid());
	EXPECT_TRUE(renderer.Get()->GetMaterial(1).IsValid());
	EXPECT_NE(renderer.Get()->GetMaterial(0).GetInstanceId(),
			  renderer.Get()->GetMaterial(1).GetInstanceId());

	UndoSystem::Undo();
	UndoSystem::Undo();
	EXPECT_EQ(renderer.Get()->GetMaterialCount(), 0u);

	UndoSystem::Terminate();
}

TEST_F(AssetCreationFixture, BindingDefaultsLeavesSlotUserAlreadyChose) {
	UndoSystem::Initialize();

	Guid generated = AssetDatabase::CreateAsset(Path("project://Generated.mat"), StringHash("MaterialAsset"));
	Guid chosen = AssetDatabase::CreateAsset(Path("project://Chosen.mat"), StringHash("MaterialAsset"));
	ASSERT_TRUE(generated.IsValid());
	ASSERT_TRUE(chosen.IsValid());

	MeshAsset mesh;
	SubMesh only;
	only.firstIndex = 0;
	only.indexCount = 3;
	only.materialSlot = 0;
	mesh.submeshes.push_back(only);
	mesh.defaultMaterials.push_back(generated);

	Handle<Scene> scene = SceneSystem::CreateScene("Keep");
	Handle<SceneObject> object = SceneSystem::CreateObject("R", scene);
	Handle<MeshRenderer> renderer = object.Get()->AddComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());

	Asset* chosenAsset = AssetDatabase::LoadAssetByGuid(chosen);
	ASSERT_NE(chosenAsset, nullptr);
	renderer.Get()->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(chosenAsset)));
	u64 chosenId = chosenAsset->GetInstanceId();

	EXPECT_EQ(EditorSceneOps::BindDefaultMaterials(Handle<Object>(renderer.Get()), mesh.GetInstanceId()), 0u);
	EXPECT_EQ(renderer.Get()->GetMaterial(0).GetInstanceId(), chosenId);

	UndoSystem::Terminate();
}
