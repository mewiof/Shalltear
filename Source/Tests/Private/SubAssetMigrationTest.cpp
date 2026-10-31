#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <filesystem>

using namespace Shalltear;

namespace {

	str LegacySibling(std::string_view kind, size_t index, std::string_view name, std::string_view extension) {
		return std::format("project://hierarchy.gltf{}/{}_{}{}", kind, index, name, extension);
	}

	std::filesystem::path MigrationRepoDir(const char* relative) {
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

	class SubAssetMigrationFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		Guid _model;
		Guid _bodyMesh;
		Guid _visorMesh;
		Guid _matA;
		Guid _matUnused;
		Guid _matC;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_submig_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));

			std::filesystem::path engineAssets = MigrationRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
			}
			AssetDatabase::Initialize();

			SceneSystem::Initialize();
			SceneSystem::Terminate();
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StageFixture(const char* name) {
			std::filesystem::path fixtures = MigrationRepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / name,
									   std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		void StageOldLayout() {
			ASSERT_TRUE(StageFixture("hierarchy.gltf"));
			_model = AssetDatabase::EnsureAssetIdentity(Path("project://hierarchy.gltf"));
			ASSERT_TRUE(_model.IsValid());

			_bodyMesh = StageDescriptor(0, "Body");
			_visorMesh = StageDescriptor(1, "Visor");
			_matA = StageMaterial(0, "MatA", 0.125f);
			_matUnused = StageMaterial(1, "MatUnused", 0.250f);
			_matC = StageMaterial(2, "MatC", 0.375f);
		}

		Guid StageDescriptor(size_t index, const char* name) {
			str virtualPath = LegacySibling(".meshes", index, name, ".slmesh");
			Path physical = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(physical.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(physical,
									  std::format("{{\n\t\"model\": \"{}\",\n\t\"mesh\": {},\n\t\"name\": \"{}\"\n}}\n",
												  _model.ToString(), index, name));
			return AssetDatabase::EnsureAssetIdentity(Path(virtualPath));
		}

		Guid StageMaterial(size_t index, const char* name, f32 marker) {
			str virtualPath = LegacySibling(".materials", index, name, ".mat");
			Path physical = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(physical.ToString()).parent_path(), ec);
			MaterialAsset draft;
			draft.SetFloat("userEditedMarker", marker);
			draft.Export(physical);
			return AssetDatabase::EnsureAssetIdentity(Path(virtualPath));
		}

		str VirtualMeshDir() const { return "project://hierarchy.gltf.meshes"; }
		str VirtualMaterialDir() const { return "project://hierarchy.gltf.materials"; }

		bool DescriptorExists(size_t index, const char* name) const {
			str virtualPath = LegacySibling(".meshes", index, name, ".slmesh");
			return FileSystem::Exists(FileSystem::Resolve(Path(virtualPath)));
		}

		bool MaterialExists(size_t index, const char* name) const {
			str virtualPath = LegacySibling(".materials", index, name, ".mat");
			return FileSystem::Exists(FileSystem::Resolve(Path(virtualPath)));
		}

		void ImportModel() {
			Asset* model = AssetDatabase::LoadAsset(Path("project://hierarchy.gltf"));
			ASSERT_NE(model, nullptr);
			AssetDatabase::UnloadAsset(model);
			AssetDatabase::CollectGarbage();
		}

		void ImportModelKeepingResidents() {
			Asset* model = AssetDatabase::LoadAsset(Path("project://hierarchy.gltf"));
			ASSERT_NE(model, nullptr);
			AssetDatabase::UnloadAsset(model);
		}

		Guid ProductGuid(std::string_view key) const {
			str text = FileSystem::ReadTextFile(
				FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://hierarchy.gltf"))), MemTag::Core);
			LinearAllocator arena(text.size() + 64, MemTag::Core);
			JSONValue root = JSONParse(text, arena);
			const JSONValue& products = root["products"];
			if (!products.IsArray()) {
				return Guid{};
			}
			for (size_t i = 0; i < products.Size(); ++i) {
				if (products[i]["key"].AsString() == key) {
					return Guid::FromString(products[i]["guid"].AsString());
				}
			}
			return Guid{};
		}

		std::vector<str> ProductKeys() const {
			std::vector<str> keys;
			str text = FileSystem::ReadTextFile(
				FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://hierarchy.gltf"))), MemTag::Core);
			LinearAllocator arena(text.size() + 64, MemTag::Core);
			JSONValue root = JSONParse(text, arena);
			const JSONValue& products = root["products"];
			if (!products.IsArray()) {
				return keys;
			}
			for (size_t i = 0; i < products.Size(); ++i) {
				keys.emplace_back(products[i]["key"].AsString());
			}
			return keys;
		}

		void StageScene() {
			Handle<Scene> scene = SceneSystem::CreateScene("Staged");
			Handle<SceneObject> saved = SceneSystem::CreateObject("Saved", scene);
			Handle<MeshRenderer> renderer = saved->AddComponent<MeshRenderer>();

			Asset* mesh = AssetDatabase::LoadAssetByGuid(_bodyMesh);
			ASSERT_NE(mesh, nullptr);
			Asset* matA = AssetDatabase::LoadAssetByGuid(_matA);
			Asset* matC = AssetDatabase::LoadAssetByGuid(_matC);
			ASSERT_NE(matA, nullptr);
			ASSERT_NE(matC, nullptr);

			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(mesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(matA)));
			renderer->SetMaterial(1, Handle<MaterialAsset>(static_cast<MaterialAsset*>(matC)));

			ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://Saved.scene")));
			SceneSystem::UnloadScene(scene);
			SceneSystem::EndFrame();

			AssetDatabase::UnloadAsset(mesh);
			AssetDatabase::UnloadAsset(matA);
			AssetDatabase::UnloadAsset(matC);
			AssetDatabase::CollectGarbage();
		}
	};

} // namespace

TEST_F(SubAssetMigrationFixture, DescriptorGuidAdopted) {
	StageOldLayout();
	ASSERT_TRUE(_bodyMesh.IsValid());
	ASSERT_TRUE(_visorMesh.IsValid());
	ImportModel();

	EXPECT_EQ(ProductGuid("Meshes/Body"), _bodyMesh);
	EXPECT_EQ(ProductGuid("Meshes/Visor"), _visorMesh);

	EXPECT_FALSE(DescriptorExists(0, "Body"));
	EXPECT_FALSE(DescriptorExists(1, "Visor"));
	EXPECT_FALSE(FileSystem::Exists(FileSystem::Resolve(Path(VirtualMeshDir() + "/0_Body.slmesh.meta"))));
}

TEST_F(SubAssetMigrationFixture, AdoptedMeshGuidStillResolvesToGeometry) {
	StageOldLayout();
	ImportModel();

	EXPECT_EQ(_bodyMesh, ProductGuid("Meshes/Body"));
	EXPECT_EQ(_visorMesh, ProductGuid("Meshes/Visor"));

	Asset* body = AssetDatabase::LoadAssetByGuid(_bodyMesh);
	ASSERT_NE(body, nullptr);
	EXPECT_EQ(body->GetAssetTypeId(), StringHash("MeshAsset"));
	EXPECT_EQ(static_cast<MeshAsset*>(body)->vertices.size(), 6u);
	AssetDatabase::UnloadAsset(body);

	Asset* visor = AssetDatabase::LoadAssetByGuid(_visorMesh);
	ASSERT_NE(visor, nullptr);
	EXPECT_EQ(static_cast<MeshAsset*>(visor)->vertices.size(), 3u);
	AssetDatabase::UnloadAsset(visor);
}

TEST_F(SubAssetMigrationFixture, GeneratedMaterialAdopted) {
	StageOldLayout();
	str before = FileSystem::ReadTextFile(
		FileSystem::Resolve(Path(VirtualMaterialDir() + "/0_MatA.mat")), MemTag::Core);
	ASSERT_FALSE(before.empty());
	ImportModel();

	EXPECT_EQ(ProductGuid("Materials/MatA"), _matA);
	EXPECT_EQ(ProductGuid("Materials/MatUnused"), _matUnused);
	EXPECT_EQ(ProductGuid("Materials/MatC"), _matC);

	EXPECT_TRUE(MaterialExists(0, "MatA"));
	EXPECT_TRUE(MaterialExists(1, "MatUnused"));
	EXPECT_TRUE(MaterialExists(2, "MatC"));
	EXPECT_EQ(FileSystem::ReadTextFile(FileSystem::Resolve(Path(VirtualMaterialDir() + "/0_MatA.mat")), MemTag::Core),
			  before);
}

TEST_F(SubAssetMigrationFixture, AdoptedMaterialUsesFile) {
	StageOldLayout();
	ImportModel();

	Asset* material = AssetDatabase::LoadAssetByGuid(_matA);
	ASSERT_NE(material, nullptr);
	ASSERT_EQ(material->GetAssetTypeId(), StringHash("MaterialAsset"));
	EXPECT_FLOAT_EQ(static_cast<MaterialAsset*>(material)->GetFloat("userEditedMarker"), 0.125f);
	AssetDatabase::UnloadAsset(material);
}

TEST_F(SubAssetMigrationFixture, StagedSceneResolvesEveryHandleAfterMigration) {
	StageOldLayout();
	StageScene();
	ImportModel();

	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://Saved.scene"));
	ASSERT_TRUE(scene.IsValid());
	Handle<SceneObject> saved = scene->FindObjectByName("Saved");
	ASSERT_TRUE(saved.IsValid());
	Handle<MeshRenderer> renderer = saved->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());

	ASSERT_TRUE(renderer->GetMesh().IsValid());
	EXPECT_EQ(renderer->GetMesh().Get()->assetId, _bodyMesh);
	EXPECT_EQ(renderer->GetMesh().Get()->vertices.size(), 6u);
	EXPECT_EQ(_bodyMesh, ProductGuid("Meshes/Body"));
	EXPECT_EQ(_matA, ProductGuid("Materials/MatA"));

	ASSERT_EQ(renderer->GetMaterialCount(), 2u);
	ASSERT_TRUE(renderer->GetMaterial(static_cast<u32>(0)).IsValid());
	EXPECT_EQ(renderer->GetMaterial(static_cast<u32>(0)).Get()->assetId, _matA);
	EXPECT_FLOAT_EQ(renderer->GetMaterial(static_cast<u32>(0)).Get()->GetFloat("userEditedMarker"), 0.125f);
	ASSERT_TRUE(renderer->GetMaterial(static_cast<u32>(1)).IsValid());
	EXPECT_EQ(renderer->GetMaterial(static_cast<u32>(1)).Get()->assetId, _matC);
}

TEST_F(SubAssetMigrationFixture, SecondImportChangesNothing) {
	StageOldLayout();
	ImportModel();
	std::vector<str> first = ProductKeys();
	ASSERT_FALSE(first.empty());

	ImportModel();
	EXPECT_EQ(ProductKeys(), first);
	EXPECT_EQ(ProductGuid("Meshes/Body"), _bodyMesh);
	EXPECT_EQ(ProductGuid("Materials/MatA"), _matA);
	EXPECT_TRUE(MaterialExists(0, "MatA"));
}

TEST_F(SubAssetMigrationFixture, FreshModelWritesNoFiles) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://hierarchy.gltf"));
	ASSERT_NE(model, nullptr);
	AssetDatabase::UnloadAsset(model);
	AssetDatabase::CollectGarbage();

	std::error_code ec;
	std::vector<str> generated;
	for (std::filesystem::recursive_directory_iterator it(_root, ec), end; it != end; it.increment(ec)) {
		if (ec) {
			break;
		}
		str relative = it->path().lexically_relative(_root).generic_string();
		if (relative.starts_with("Library/") || relative == "Library") {
			continue;
		}
		str name = it->path().filename().string();
		if (name == "hierarchy.gltf" || name == "hierarchy.gltf.meta" || name == ".assets.db") {
			continue;
		}
		generated.push_back(relative);
	}
	EXPECT_TRUE(generated.empty()) << generated.size() << ' ' << (generated.empty() ? str("<none>") : generated.front());

	EXPECT_TRUE(ProductGuid("Meshes/Body").IsValid());
	EXPECT_TRUE(ProductGuid("Meshes/Visor").IsValid());
	EXPECT_TRUE(ProductGuid("Materials/MatA").IsValid());
}

TEST_F(SubAssetMigrationFixture, ReimportKeepsExtractedMaterial) {
	StageOldLayout();
	ImportModel();

	Asset* first = AssetDatabase::LoadAssetByGuid(_matA);
	ASSERT_NE(first, nullptr);
	EXPECT_FLOAT_EQ(static_cast<MaterialAsset*>(first)->GetFloat("userEditedMarker"), 0.125f);
	AssetDatabase::UnloadAsset(first);

	ImportModelKeepingResidents();

	Asset* second = AssetDatabase::LoadAssetByGuid(_matA);
	ASSERT_NE(second, nullptr);
	EXPECT_FLOAT_EQ(static_cast<MaterialAsset*>(second)->GetFloat("userEditedMarker"), 0.125f);
	AssetDatabase::UnloadAsset(second);
}

TEST_F(SubAssetMigrationFixture, StaleDescriptorKeepsKey) {
	StageOldLayout();
	ImportModel();
	ASSERT_EQ(ProductGuid("Meshes/Body"), _bodyMesh);
	ASSERT_FALSE(DescriptorExists(0, "Body"));

	str legacy = LegacySibling(".meshes", 0, "Body", ".slmesh");
	Path physical = FileSystem::Resolve(Path(legacy));
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::path(physical.ToString()).parent_path(), ec);
	FileSystem::WriteTextFile(physical, std::format("{{\n\t\"model\": \"{}\",\n\t\"mesh\": 0,\n\t\"name\": \"Body\"\n}}\n", _model.ToString()));
	Guid stranger = AssetDatabase::GenerateGuid();
	ASSERT_NE(stranger, _bodyMesh);
	FileSystem::WriteTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path(legacy))),
							  std::format("{{\n\t\"assetId\": \"{}\",\n\t\"type\": \"0000000000000000\"\n}}\n", stranger.ToString()));

	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	ASSERT_EQ(AssetDatabase::EnsureAssetIdentity(Path(legacy)), stranger);

	ImportModel();

	EXPECT_EQ(ProductGuid("Meshes/Body"), _bodyMesh);
	size_t rows = 0;
	for (const str& key : ProductKeys()) {
		rows += (key == "Meshes/Body") ? 1u : 0u;
	}
	EXPECT_EQ(rows, 1u);
}
