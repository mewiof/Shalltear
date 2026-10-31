#include "TestsPCH.h"

#include "glTFParser.h"
#include "ModelImport.h"
#include "ProductLookup.h"
#include "Scene/SceneDocument.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Graphics/TextureCodecs.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path RepoDir(const char* relative) {
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

	class GraphicsImportFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_gfx_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));

			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
			}
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StageMeshDescriptor(const std::filesystem::path& at, const Guid& model, i64 meshIndex, std::string_view name) {
			std::error_code ec;
			std::filesystem::create_directories(at.parent_path(), ec);
			return FileSystem::WriteTextFile(
				Path(at.generic_string()),
				std::format("{{\n\t\"model\": \"{}\",\n\t\"mesh\": {},\n\t\"name\": \"{}\"\n}}\n", model.ToString(), meshIndex, name));
		}

		Guid ImportModel(const char* virtualPath) {
			Asset* model = AssetDatabase::LoadAsset(Path(virtualPath));
			if (!model) {
				return {};
			}
			Guid guid = model->assetId;
			AssetDatabase::UnloadAsset(model);
			AssetDatabase::CollectGarbage();
			return guid;
		}

		PrefabAsset* LoadModelPrefab(const char* virtualPath) {
			Asset* loaded = AssetDatabase::LoadAsset(Path(virtualPath));
			if (!loaded || loaded->GetAssetTypeId() != StringHash("PrefabAsset")) {
				return nullptr;
			}
			return static_cast<PrefabAsset*>(loaded);
		}

		template <typename T>
		T* ReadRow(const SceneDocumentBlob& blob) {
			T* instance = static_cast<T*>(TypeRegistry::CreateObjectInstance(T::GetReflectType(), MemTag::Scene));
			TextSerializer::Deserialize(instance, T::GetReflectType(), JSONSerialize(blob.json, false));
			return instance;
		}

		const SceneDocumentRow* FindRow(const SceneDocument& fragment, u64 id) {
			for (const SceneDocumentRow& row : fragment.rows) {
				if (row.id == id) {
					return &row;
				}
			}
			return nullptr;
		}

		bool AppendNodeToStagedModel(const char* virtualPath, std::string_view name) {
			str text = FileSystem::ReadTextFile(Path(virtualPath));
			const str childrenBefore = "\"children\": [\n    1,\n    2,\n    3,\n    4\n   ]";
			const str childrenAfter = "\"children\": [\n    1,\n    2,\n    3,\n    4,\n    5\n   ]";
			const str nodesEnd = " ],\n \"meshes\": [";
			size_t children = text.find(childrenBefore);
			if (children == str::npos || text.find(nodesEnd) == str::npos) {
				return false;
			}
			text.replace(children, childrenBefore.size(), childrenAfter);
			text.insert(text.find(nodesEnd),
						std::format(",\n  {{\n   \"name\": \"{}\",\n   \"translation\": [0.0, 4.0, 0.0]\n  }}\n", name));
			return FileSystem::WriteTextFile(Path(virtualPath), text);
		}

		bool ReimportUnreferencedModel(const char* virtualPath) {
			AssetDatabase::CollectGarbage();
			Asset* fresh = AssetDatabase::LoadAsset(Path(virtualPath));
			if (!fresh) {
				return false;
			}
			AssetDatabase::UnloadAsset(fresh);
			AssetDatabase::CollectGarbage();
			return true;
		}

		bool StageFixture(const char* name) {
			std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / name,
									   std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}
	};

} // anonymous namespace

TEST_F(GraphicsImportFixture, PNGDecodesWithDefaults) {
	ASSERT_TRUE(StageFixture("checker.png"));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://checker.png"));
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->GetAssetTypeId(), StringHash("TextureAsset"));
	TextureAsset* texture = static_cast<TextureAsset*>(loaded);

	EXPECT_EQ(texture->size, (Vec2I{8, 8}));
	EXPECT_EQ(texture->format, RHI::Format::BC7SRGB);
	EXPECT_EQ(texture->storedMipLevels, 4u);
	const TextureCodec* codec = TextureCodecs::Find(texture->format);
	ASSERT_NE(codec, nullptr);
	u8 decoded[8 * 8 * 4] = {};
	ASSERT_TRUE(codec->decode(texture->format, {texture->pixels.data(), 64}, {8, 8}, decoded));
	EXPECT_NEAR(decoded[0], 255, 2);
	EXPECT_NEAR(decoded[1], 32, 2);
	EXPECT_NEAR(decoded[2], 32, 2);
	EXPECT_EQ(decoded[3], 255);

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, MetaSettingsDriveTheImport) {
	ASSERT_TRUE(StageFixture("checker.png"));

	Asset* first = AssetDatabase::LoadAsset(Path("project://checker.png"));
	ASSERT_NE(first, nullptr);
	Guid guid = first->assetId;
	AssetDatabase::UnloadAsset(first);

	Path metaPath = FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://checker.png")));
	str metaText = FileSystem::ReadTextFile(metaPath, MemTag::Asset);
	EXPECT_NE(metaText.find("sRGB"), str::npos);
	EXPECT_NE(metaText.find("generateMips"), str::npos);

	size_t keyAt = metaText.find("sRGB");
	ASSERT_NE(keyAt, str::npos);
	size_t valueAt = metaText.find("true", keyAt);
	ASSERT_NE(valueAt, str::npos);
	metaText.replace(valueAt, 4, "false");
	FileSystem::WriteTextFile(metaPath, metaText);
	AssetDatabase::CollectGarbage();

	Asset* second = AssetDatabase::LoadAsset(Path("project://checker.png"));
	ASSERT_NE(second, nullptr);
	EXPECT_EQ(second->assetId, guid);
	EXPECT_EQ(static_cast<TextureAsset*>(second)->format, RHI::Format::BC7Unorm);
	AssetDatabase::UnloadAsset(second);
}

TEST_F(GraphicsImportFixture, GlbMakesMeshAndMaterial) {
	ASSERT_TRUE(StageFixture("cube.glb"));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://cube.glb"));
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->GetAssetTypeId(), StringHash("PrefabAsset"));
	EXPECT_FALSE(std::filesystem::exists(_root / "cube.glb.meshes"));

	Asset* meshAsset = ProductLookup::Load(Path("project://cube.glb"), "Meshes/Cube");
	ASSERT_NE(meshAsset, nullptr);
	ASSERT_EQ(meshAsset->GetAssetTypeId(), StringHash("MeshAsset"));
	MeshAsset* mesh = static_cast<MeshAsset*>(meshAsset);

	EXPECT_EQ(mesh->vertices.size(), 24u);
	EXPECT_EQ(mesh->indices.size(), 36u);
	ASSERT_EQ(mesh->submeshes.size(), 1u);
	EXPECT_EQ(mesh->submeshes[0].indexCount, 36u);
	EXPECT_EQ(mesh->submeshes[0].materialSlot, 0u);
	EXPECT_TRUE(Vec3::Approximately(mesh->bounds.min, Vec3(-0.5f, -0.5f, -0.5f)));
	EXPECT_TRUE(Vec3::Approximately(mesh->bounds.max, Vec3(0.5f, 0.5f, 0.5f)));

	EXPECT_TRUE(std::filesystem::exists(_root / "cube.glb.images" / "0_checker.png"));
	EXPECT_FALSE(std::filesystem::exists(_root / "cube.glb.materials"));
	ASSERT_EQ(mesh->defaultMaterials.size(), 1u);
	EXPECT_TRUE(mesh->defaultMaterials[0].IsValid());
	EXPECT_EQ(mesh->defaultMaterials[0], ProductLookup::Find(Path("project://cube.glb"), "Materials/CheckerMat"));

	Asset* materialAsset = AssetDatabase::LoadAssetByGuid(mesh->defaultMaterials[0]);
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);
	EXPECT_TRUE(Color::Approximately(material->GetColor(StringHash("baseColor")), Color::White()));
	EXPECT_TRUE(material->GetTexture(StringHash("baseColorTexture")).IsValid());
	EXPECT_TRUE(material->shaderGuid.IsValid());
	EXPECT_TRUE(material->shader.IsValid());
	AssetDatabase::UnloadAsset(materialAsset);

	AssetDatabase::UnloadAsset(meshAsset);
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, GeneratedMaterialsAreNeverOverwritten) {
	ASSERT_TRUE(StageFixture("cube.glb"));

	Path materialPath = Path((_root / "cube.glb.materials" / "0_CheckerMat.mat").generic_string());
	std::error_code ec;
	std::filesystem::create_directories(materialPath.GetDirectory(), ec);
	{
		MaterialAsset staged;
		staged.SetColor("baseColor", Color::Red());
		ASSERT_TRUE(staged.Export(materialPath));
	}
	Guid extracted = AssetDatabase::EnsureAssetIdentity(Path("project://cube.glb.materials/0_CheckerMat.mat"));
	ASSERT_TRUE(extracted.IsValid());

	Asset* model = AssetDatabase::LoadAsset(Path("project://cube.glb"));
	ASSERT_NE(model, nullptr);
	AssetDatabase::UnloadAsset(model);
	AssetDatabase::CollectGarbage();

	EXPECT_EQ(ProductLookup::Find(Path("project://cube.glb"), "Materials/CheckerMat"), extracted);

	MaterialAsset probe;
	ASSERT_TRUE(probe.Import(materialPath));
	EXPECT_TRUE(Color::Approximately(probe.GetColor(StringHash("baseColor")), Color::Red()));

	Asset* resolved = AssetDatabase::LoadAssetByGuid(extracted);
	ASSERT_NE(resolved, nullptr);
	EXPECT_TRUE(Color::Approximately(static_cast<MaterialAsset*>(resolved)->GetColor(StringHash("baseColor")), Color::Red()));
	AssetDatabase::UnloadAsset(resolved);
}

TEST_F(GraphicsImportFixture, ExternalGLTFWithBase64BufferImports) {
	ASSERT_TRUE(StageFixture("cube_external.gltf"));
	ASSERT_TRUE(StageFixture("checker.png"));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://cube_external.gltf"));
	ASSERT_NE(loaded, nullptr);
	EXPECT_FALSE(std::filesystem::exists(_root / "cube_external.gltf.images"));

	Asset* meshAsset = ProductLookup::Load(Path("project://cube_external.gltf"), "Meshes/Cube");
	ASSERT_NE(meshAsset, nullptr);
	MeshAsset* mesh = static_cast<MeshAsset*>(meshAsset);
	EXPECT_EQ(mesh->vertices.size(), 24u);
	EXPECT_EQ(mesh->indices.size(), 36u);
	ASSERT_EQ(mesh->defaultMaterials.size(), 1u);
	EXPECT_TRUE(mesh->defaultMaterials[0].IsValid());
	AssetDatabase::UnloadAsset(meshAsset);
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, TruncatedGlbIsRejected) {
	ASSERT_TRUE(StageFixture("cube.glb"));
	std::filesystem::path glb = _root / "cube.glb";
	std::filesystem::resize_file(glb, 100);

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://cube.glb"));
	EXPECT_EQ(loaded, nullptr);
}

TEST_F(GraphicsImportFixture, MaterialRoundTripsThroughMat) {
	MaterialAsset original;
	original.shaderGuid = Guid{0x1234, 0x5678};
	original.SetColor("baseColor", Color(0.25f, 0.5f, 0.75f, 1.0f));
	original.SetFloat("specular", 0.4f);
	original.SetFloat("shininess", 64.0f);
	original.SetTexture("baseColorTexture", Guid{0xAB, 0xCD});

	Path file = Path((_root / "roundtrip.mat").generic_string());
	ASSERT_TRUE(original.Export(file));

	MaterialAsset restored;
	ASSERT_TRUE(restored.Import(file));
	EXPECT_EQ(restored.shaderGuid, (Guid{0x1234, 0x5678}));
	EXPECT_TRUE(Color::Approximately(restored.GetColor(StringHash("baseColor")), Color(0.25f, 0.5f, 0.75f, 1.0f)));
	EXPECT_FLOAT_EQ(restored.GetFloat(StringHash("specular")), 0.4f);
	EXPECT_FLOAT_EQ(restored.GetFloat(StringHash("shininess")), 64.0f);
	ASSERT_EQ(restored.textures.size(), 1u);
	EXPECT_EQ(restored.textures[0].texture, (Guid{0xAB, 0xCD}));

	EXPECT_FLOAT_EQ(restored.GetFloat(StringHash("nonexistent"), 7.0f), 7.0f);
}

TEST_F(GraphicsImportFixture, MalformedMatIsRejected) {
	Path file = Path((_root / "broken.mat").generic_string());
	FileSystem::WriteTextFile(file, "not json {{{");
	MaterialAsset material;
	EXPECT_FALSE(material.Import(file));
}

TEST_F(GraphicsImportFixture, EngineShadersImport) {
	std::filesystem::path engineAssets = RepoDir("EngineAssets");
	if (engineAssets.empty()) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	Asset* shader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	if (!shader) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	ShaderAsset* standard = static_cast<ShaderAsset*>(shader);
	EXPECT_FALSE(standard->variants[SHADER_BASE_VARIANT].vertexSpirv.empty());
	EXPECT_FALSE(standard->variants[SHADER_BASE_VARIANT].pixelSpirv.empty());
	EXPECT_EQ(standard->pushConstantSize, 16u);
	EXPECT_EQ(standard->renderState.blend, RHI::BlendMode::Opaque);
	EXPECT_EQ(standard->bindings.size(), 21u);
	AssetDatabase::UnloadAsset(shader);

	Asset* wireAsset = AssetDatabase::LoadAsset(Path("engine://Shaders/Wireframe.hlsl"));
	ASSERT_NE(wireAsset, nullptr);
	EXPECT_EQ(static_cast<ShaderAsset*>(wireAsset)->pushConstantSize, 20u);
	AssetDatabase::UnloadAsset(wireAsset);
}

TEST_F(GraphicsImportFixture, NodeGraphKeepsHierarchy) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "hierarchy.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;

	ASSERT_EQ(parsed.nodes.size(), 5u);
	EXPECT_EQ(parsed.nodes[0].name, "Root");
	EXPECT_EQ(parsed.nodes[1].name, "Body");
	EXPECT_EQ(parsed.nodes[2].name, "Muzzle");
	EXPECT_EQ(parsed.nodes[3].name, "Sheared");
	EXPECT_EQ(parsed.nodes[4].name, "VisorCopy");

	EXPECT_EQ(parsed.nodes[0].parent, -1);
	for (size_t i = 1; i < parsed.nodes.size(); ++i) {
		EXPECT_EQ(parsed.nodes[i].parent, 0) << "node " << i;
		EXPECT_LT(parsed.nodes[i].parent, static_cast<i32>(i)) << "node " << i;
	}

	EXPECT_EQ(parsed.nodes[2].mesh, -1);
	EXPECT_EQ(parsed.nodes[1].mesh, 0);
}

TEST_F(GraphicsImportFixture, NodeTransformsStayLocal) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "hierarchy.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;

	EXPECT_NEAR(parsed.nodes[0].position.x, -1.0f, 1e-5f);
	EXPECT_NEAR(parsed.nodes[0].position.y, 2.0f, 1e-5f);
	EXPECT_NEAR(parsed.nodes[0].position.z, 3.0f, 1e-5f);
	EXPECT_NEAR(parsed.nodes[0].scale.x, 1.0f, 1e-5f);

	EXPECT_NEAR(parsed.nodes[1].scale.x, 2.0f, 1e-5f);
	EXPECT_NEAR(parsed.nodes[1].position.x, 0.0f, 1e-5f);
	EXPECT_NEAR(parsed.nodes[1].position.y, 0.0f, 1e-5f);
	EXPECT_NEAR(parsed.nodes[1].position.z, 0.0f, 1e-5f);

	ASSERT_EQ(parsed.meshes.size(), 2u);
	const ModelMesh& body = parsed.meshes[0];
	ASSERT_EQ(body.vertices.size(), 6u);
	EXPECT_NEAR(body.vertices[1].position.x, -1.0f, 1e-5f);
	EXPECT_NEAR(body.vertices[1].position.y, 0.0f, 1e-5f);
	EXPECT_NEAR(body.vertices[1].position.z, 0.0f, 1e-5f);
	EXPECT_NEAR(body.bounds.min.x, -1.0f, 1e-5f);
	EXPECT_NEAR(body.bounds.max.z, 1.0f, 1e-5f);
}

TEST_F(GraphicsImportFixture, AMaterialSlotIsDensePerMesh) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "hierarchy.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.materials.size(), 3u);

	const ModelMesh& body = parsed.meshes[0];
	ASSERT_EQ(body.submeshes.size(), 2u);
	EXPECT_EQ(body.submeshes[0].materialSlot, 0u);
	EXPECT_EQ(body.submeshes[1].materialSlot, 1u);
	ASSERT_EQ(body.materials.size(), 2u);
	EXPECT_EQ(body.materials[0], 2);
	EXPECT_EQ(body.materials[1], 0);

	const ModelMesh& visor = parsed.meshes[1];
	ASSERT_EQ(visor.submeshes.size(), 1u);
	EXPECT_EQ(visor.submeshes[0].materialSlot, 0u);
	ASSERT_EQ(visor.materials.size(), 1u);
	EXPECT_EQ(visor.materials[0], 2);
}

TEST_F(GraphicsImportFixture, GeneratedMaterialBindsMaps) {
	ASSERT_TRUE(StageFixture("cube_external.gltf"));
	ASSERT_TRUE(StageFixture("checker.png"));
	std::error_code ec;
	for (const char* copy : {"mr.png", "nrm.png", "occ.png", "emis.png"}) {
		std::filesystem::copy_file(_root / "checker.png", _root / copy,
								   std::filesystem::copy_options::overwrite_existing, ec);
		ASSERT_FALSE(ec) << copy;
	}

	str document = FileSystem::ReadTextFile(Path((_root / "cube_external.gltf").generic_string()), MemTag::Asset);
	ASSERT_FALSE(document.empty());
	const size_t tail = document.find("\"materials\"");
	ASSERT_NE(tail, str::npos);
	document.resize(tail);
	document += R"("materials": [
  {
   "name": "CheckerMat",
   "pbrMetallicRoughness": {
    "baseColorFactor": [ 1.0, 1.0, 1.0, 1.0 ],
    "baseColorTexture": { "index": 0 },
    "metallicFactor": 0.25,
    "roughnessFactor": 0.8,
    "metallicRoughnessTexture": { "index": 1 }
   },
   "normalTexture": { "index": 2, "scale": 0.5 },
   "occlusionTexture": { "index": 3, "strength": 0.25 },
   "emissiveTexture": { "index": 4 },
   "emissiveFactor": [ 0.1, 0.2, 0.3 ]
  }
 ],
 "textures": [ { "source": 0 }, { "source": 1 }, { "source": 2 }, { "source": 3 }, { "source": 4 } ],
 "images": [
  { "uri": "checker.png", "name": "checker" },
  { "uri": "mr.png", "name": "mr" },
  { "uri": "nrm.png", "name": "nrm" },
  { "uri": "occ.png", "name": "occ" },
  { "uri": "emis.png", "name": "emis" }
 ]
})";
	ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / "cube_external.gltf").generic_string()), document));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://cube_external.gltf"));
	ASSERT_NE(loaded, nullptr);
	Asset* materialAsset = ProductLookup::Load(Path("project://cube_external.gltf"), "Materials/CheckerMat");
	ASSERT_NE(materialAsset, nullptr);
	MaterialAsset* material = static_cast<MaterialAsset*>(materialAsset);

	EXPECT_EQ(material->shaderGuid, AssetDatabase::EnsureAssetIdentity(Path("engine://Shaders/Lit.hlsl")));

	EXPECT_NEAR(material->GetFloat(StringHash("metallic")), 0.25f, 1e-6f);
	EXPECT_NEAR(material->GetFloat(StringHash("roughness")), 0.8f, 1e-6f);
	EXPECT_NEAR(material->GetFloat(StringHash("normalScale")), 0.5f, 1e-6f);
	EXPECT_NEAR(material->GetFloat(StringHash("occlusionStrength")), 0.25f, 1e-6f);
	const MaterialAsset::Param* emissive = material->FindParam(StringHash("emissive"));
	ASSERT_NE(emissive, nullptr);
	EXPECT_NEAR(emissive->numbers[0], 0.1f, 1e-6f);
	EXPECT_NEAR(emissive->numbers[1], 0.2f, 1e-6f);
	EXPECT_NEAR(emissive->numbers[2], 0.3f, 1e-6f);
	EXPECT_FALSE(material->HasFloat(StringHash("specular")));
	EXPECT_FALSE(material->HasFloat(StringHash("shininess")));

	const char* slots[] = {"baseColorTexture", "metallicRoughnessTexture", "normalTexture",
						   "occlusionTexture", "emissiveTexture"};
	Vector<Guid> bound{GetMemoryResource(MemTag::Asset)};
	for (const char* slot : slots) {
		const MaterialAsset::TextureParam* found = nullptr;
		for (const MaterialAsset::TextureParam& texture : material->textures) {
			if (texture.hash == StringHash(slot)) {
				found = &texture;
			}
		}
		ASSERT_NE(found, nullptr) << slot;
		EXPECT_TRUE(found->texture.IsValid()) << slot;
		EXPECT_EQ(std::find(bound.begin(), bound.end(), found->texture), bound.end()) << slot;
		bound.push_back(found->texture);
	}

	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, GltfMaterialCarriesMapsAndFactors) {
	ASSERT_TRUE(StageFixture("BoomBoxWithAxes.gltf"));
	ASSERT_TRUE(StageFixture("BoomBoxWithAxes.bin"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "BoomBoxWithAxes.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.materials.size(), 2u);
	ASSERT_EQ(parsed.images.size(), 5u);

	const ModelMaterial& boomBox = parsed.materials[0];
	EXPECT_EQ(boomBox.name, "M_BoomBox");
	EXPECT_EQ(boomBox.baseColorImage, 0);
	EXPECT_EQ(boomBox.metallicRoughnessImage, 1);
	EXPECT_EQ(boomBox.normalImage, 2);
	EXPECT_EQ(boomBox.emissiveImage, 3);
	EXPECT_EQ(boomBox.occlusionImage, -1);

	EXPECT_NEAR(boomBox.emissiveFactor.x, 1.0f, 1e-6f);
	EXPECT_NEAR(boomBox.emissiveFactor.y, 1.0f, 1e-6f);
	EXPECT_NEAR(boomBox.emissiveFactor.z, 1.0f, 1e-6f);

	EXPECT_NEAR(boomBox.metallicFactor, 1.0f, 1e-6f);
	EXPECT_NEAR(boomBox.roughnessFactor, 1.0f, 1e-6f);

	const ModelMaterial& axes = parsed.materials[1];
	EXPECT_EQ(axes.name, "M_Coordinates");
	EXPECT_EQ(axes.baseColorImage, 4);
	EXPECT_EQ(axes.metallicRoughnessImage, -1);
	EXPECT_EQ(axes.normalImage, -1);
	EXPECT_EQ(axes.occlusionImage, -1);
	EXPECT_EQ(axes.emissiveImage, -1);
	EXPECT_NEAR(axes.metallicFactor, 0.0f, 1e-6f);
	EXPECT_NEAR(axes.roughnessFactor, 0.735f, 1e-6f);
	EXPECT_NEAR(axes.emissiveFactor.x + axes.emissiveFactor.y + axes.emissiveFactor.z, 0.0f, 1e-6f);
}

TEST_F(GraphicsImportFixture, DanglingTextureBindsNothing) {
	const char* document = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "name": "Empty" } ],
  "images": [ { "uri": "a.png" }, { "uri": "b.png" } ],
  "textures": [ { "source": 0 }, { "source": 1 }, { "source": 9 } ],
  "materials": [
    {
      "name": "Scaled",
      "normalTexture": { "index": 0, "scale": 0.25 },
      "occlusionTexture": { "index": 1, "strength": 0.75 }
    },
    {
      "name": "Defaulted",
      "normalTexture": { "index": 1 },
      "occlusionTexture": { "index": 0 }
    },
    {
      "name": "Dangling",
      "pbrMetallicRoughness": { "baseColorTexture": { "index": 2 } },
      "normalTexture": { "index": 7 }
    }
  ]
})";
	ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / "scalars.gltf").generic_string()), document));
	ModelParseResult parsed = ParseGLTF(Path((_root / "scalars.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.materials.size(), 3u);

	EXPECT_NEAR(parsed.materials[0].normalScale, 0.25f, 1e-6f);
	EXPECT_NEAR(parsed.materials[0].occlusionStrength, 0.75f, 1e-6f);
	EXPECT_EQ(parsed.materials[0].normalImage, 0);
	EXPECT_EQ(parsed.materials[0].occlusionImage, 1);

	EXPECT_NEAR(parsed.materials[1].normalScale, 1.0f, 1e-6f);
	EXPECT_NEAR(parsed.materials[1].occlusionStrength, 1.0f, 1e-6f);

	EXPECT_EQ(parsed.materials[2].baseColorImage, -1);
	EXPECT_EQ(parsed.materials[2].normalImage, -1);
}

namespace {

	constexpr const char* ROLES_DOCUMENT = R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "name": "Empty" } ],
  "images": [ { "uri": "n.png" }, { "uri": "m.png" }, { "uri": "c.png" }, { "uri": "x.png" } ],
  "textures": [ { "source": 0 }, { "source": 1 }, { "source": 2 }, { "source": 3 } ],
  "materials": [
    {
      "name": "Mapped",
      "pbrMetallicRoughness": { "baseColorTexture": { "index": 2 }, "metallicRoughnessTexture": { "index": 1 } },
      "normalTexture": { "index": 0 },
      "occlusionTexture": { "index": 1 }
    },
    {
      "name": "Confused",
      "pbrMetallicRoughness": { "baseColorTexture": { "index": 3 } },
      "normalTexture": { "index": 3 }
    }
  ]
})";

	struct ImageSettings {
		str type;
		bool sRGB = true;
		str filter;
	};

	ImageSettings SidecarOf(const char* image) {
		const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path(str("project://") + image))), MemTag::Asset);
		LinearAllocator arena(text.size() + 64, MemTag::Asset);
		const JSONValue root = JSONParse(text, arena);
		const JSONValue& settings = root["importer"]["settings"];
		ImageSettings out;
		out.type = str(settings["textureType"].AsString(""));
		out.sRGB = settings["sRGB"].AsBool(true);
		out.filter = str(settings["filter"].AsString(""));
		return out;
	}

} // anonymous namespace

TEST_F(GraphicsImportFixture, ModelSetsImageSidecars) {
	ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / "roles.gltf").generic_string()), ROLES_DOCUMENT));
	for (const char* image : {"n.png", "m.png", "c.png", "x.png"}) {
		ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / image).generic_string()), "not decoded"));
	}
	Asset* model = AssetDatabase::LoadAsset(Path("project://roles.gltf"));
	ASSERT_NE(model, nullptr);
	AssetDatabase::UnloadAsset(model);

	const ImageSettings normal = SidecarOf("n.png");
	EXPECT_EQ(normal.type, "normalMap");
	EXPECT_FALSE(normal.sRGB);
	const ImageSettings data = SidecarOf("m.png");
	EXPECT_EQ(data.type, "default");
	EXPECT_FALSE(data.sRGB);
	const ImageSettings color = SidecarOf("c.png");
	EXPECT_EQ(color.type, "default");
	EXPECT_TRUE(color.sRGB);
	const ImageSettings confused = SidecarOf("x.png");
	EXPECT_EQ(confused.type, "default");
	EXPECT_TRUE(confused.sRGB);
}

TEST_F(GraphicsImportFixture, ModelFixesOldSidecarOnly) {
	ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / "roles.gltf").generic_string()), ROLES_DOCUMENT));
	for (const char* image : {"n.png", "m.png", "c.png", "x.png"}) {
		ASSERT_TRUE(FileSystem::WriteTextFile(Path((_root / image).generic_string()), "not decoded"));
		ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(Path(str("project://") + image)).IsValid());
	}
	JSONValue preM15 = JSONValue::MakeObject();
	preM15.AddMember("filter", JSONValue::MakeString("nearest"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://n.png"), preM15));
	JSONValue authored = JSONValue::MakeObject();
	authored.AddMember("textureType", JSONValue::MakeString("default"));
	authored.AddMember("sRGB", JSONValue::MakeBool(true));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(Path("project://m.png"), authored));

	Asset* model = AssetDatabase::LoadAsset(Path("project://roles.gltf"));
	ASSERT_NE(model, nullptr);
	AssetDatabase::UnloadAsset(model);

	const ImageSettings normal = SidecarOf("n.png");
	EXPECT_EQ(normal.type, "normalMap");
	EXPECT_FALSE(normal.sRGB);
	EXPECT_EQ(normal.filter, "nearest");
	const ImageSettings data = SidecarOf("m.png");
	EXPECT_EQ(data.type, "default");
	EXPECT_TRUE(data.sRGB);
}

TEST_F(GraphicsImportFixture, Mesh2NodesShareIsStoredOnce) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "hierarchy.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;

	EXPECT_EQ(parsed.nodes[3].mesh, 1);
	EXPECT_EQ(parsed.nodes[4].mesh, 1);
	EXPECT_EQ(parsed.meshes.size(), 2u);
	EXPECT_EQ(parsed.meshes[1].name, "Visor");
}

TEST_F(GraphicsImportFixture, ShearedMatrixFlagged) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "hierarchy.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;

	EXPECT_TRUE(parsed.nodes[3].shearedMatrix);
	EXPECT_NEAR(parsed.nodes[3].localMatrix(0, 1), -1.0f, 1e-5f);

	EXPECT_FALSE(parsed.nodes[0].shearedMatrix);
	EXPECT_FALSE(parsed.nodes[1].shearedMatrix);
	EXPECT_FALSE(parsed.nodes[2].shearedMatrix);
	EXPECT_FALSE(parsed.nodes[4].shearedMatrix);
}

TEST_F(GraphicsImportFixture, SharedFrameSkinIsIdentity) {
	ASSERT_TRUE(StageFixture("SimpleSkin.gltf"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "SimpleSkin.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ModelRig rig = BuildModelRig(parsed);
	ASSERT_FALSE(rig.IsEmpty());

	const ModelSkinFrame frame = FindSkinFrame(parsed, std::span<const i32>(rig.nodeToBone.data(), rig.nodeToBone.size()));
	EXPECT_EQ(frame.skin, 0);
	EXPECT_TRUE(Mat4::Approximately(frame.meshToRig, Mat4::Identity(), 1e-4f)) << frame.meshToRig.ToString();
}

TEST_F(GraphicsImportFixture, SkinFrameCarriesMeshPlacement) {
	ASSERT_TRUE(StageFixture("HumanoidRig.gltf"));
	{
		const std::filesystem::path staged = _root / "HumanoidRig.gltf";
		std::string text;
		{
			std::ifstream input(staged, std::ios::binary);
			text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		}
		const std::string host = "\"name\": \"Body\",\n      \"mesh\": 0,";
		const size_t at = text.find(host);
		ASSERT_NE(at, std::string::npos);
		text.replace(at, host.size(), "\"name\": \"Body\",\n      \"translation\": [0.0, 1.0, 0.0],\n      \"mesh\": 0,");
		std::ofstream output(staged, std::ios::binary | std::ios::trunc);
		output.write(text.data(), static_cast<std::streamsize>(text.size()));
		ASSERT_TRUE(output.good());
	}

	ModelParseResult parsed = ParseGLTF(Path((_root / "HumanoidRig.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ModelRig rig = BuildModelRig(parsed);
	ASSERT_FALSE(rig.IsEmpty());

	const ModelSkinFrame frame = FindSkinFrame(parsed, std::span<const i32>(rig.nodeToBone.data(), rig.nodeToBone.size()));
	ASSERT_EQ(frame.skin, 0);
	EXPECT_TRUE(Vec3::Approximately(frame.meshToRig.TransformPoint(Vec3()), Vec3(0.0f, 1.0f, 0.0f), 1e-4f));
}

TEST_F(GraphicsImportFixture, SkinFrameRefusesSkinless) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "hierarchy.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_TRUE(parsed.skins.empty());

	ModelRig rig = BuildModelRig(parsed);
	EXPECT_TRUE(rig.IsEmpty());
	EXPECT_EQ(FindSkinFrame(parsed, std::span<const i32>(rig.nodeToBone.data(), rig.nodeToBone.size())).skin, -1);
}

TEST_F(GraphicsImportFixture, ShearedNodeKeepsClosestRotation) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ModelParseResult parsed = ParseGLTF(Path((_root / "hierarchy.gltf").generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_TRUE(parsed.nodes[3].shearedMatrix);

	EXPECT_NEAR(parsed.nodes[3].rotation.Length(), 1.0f, 1e-4f);
	EXPECT_GT(Quat::Angle(parsed.nodes[3].rotation, Quat::Identity()), 5.0f);
}

TEST_F(GraphicsImportFixture, ModelAssetIsModelPrefab) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://hierarchy.gltf"));
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->GetAssetTypeId(), StringHash("PrefabAsset"));
	PrefabAsset* prefab = static_cast<PrefabAsset*>(loaded);
	EXPECT_EQ(prefab->GetKind(), PrefabKind::Model);
	EXPECT_FALSE(prefab->GetBasePrefab().IsValid());

	str meta = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://hierarchy.gltf"))), MemTag::Asset);
	EXPECT_NE(meta.find(std::format("{:016x}", StringHash("PrefabAsset").GetValue())), str::npos) << meta;

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, ModelExtensionsRegistered) {
	Vector<std::string_view> extensions = ModelExtensions();
	ASSERT_FALSE(extensions.empty());
	for (std::string_view extension : extensions) {
		IAssetImporter* importer = AssetImporterRegistry::FindByExtension(extension);
		ASSERT_NE(importer, nullptr) << extension;
		EXPECT_EQ(importer->GetAssetType(), StringHash("PrefabAsset")) << extension;
	}
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("PrefabAsset")), nullptr);
}

TEST_F(GraphicsImportFixture, DeepNodeChainWalkedIteratively) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));

	std::filesystem::path source = _root / "hierarchy.gltf";
	str document = FileSystem::ReadTextFile(Path(source.generic_string()), MemTag::Asset);
	ASSERT_FALSE(document.empty());

	constexpr int DEPTH = 10000;
	str nodes = "[";
	for (int i = 0; i < DEPTH; ++i) {
		if (i + 1 < DEPTH) {
			nodes += std::format("{{\"name\":\"n{}\",\"children\":[{}]}},", i, i + 1);
		} else {
			nodes += std::format("{{\"name\":\"n{}\",\"mesh\":0}}", i);
		}
	}
	nodes += "]";

	size_t meshesAt = document.find("\"meshes\"");
	ASSERT_NE(meshesAt, str::npos);
	size_t nodesAt = document.rfind("\"nodes\"", meshesAt);
	ASSERT_NE(nodesAt, str::npos);
	size_t open = document.find('[', nodesAt);
	ASSERT_NE(open, str::npos);
	int depth = 0;
	size_t close = open;
	for (; close < document.size(); ++close) {
		if (document[close] == '[') {
			++depth;
		} else if (document[close] == ']' && --depth == 0) {
			break;
		}
	}
	ASSERT_LT(close, document.size());
	document = document.substr(0, open) + nodes + document.substr(close + 1);

	std::filesystem::path deep = _root / "deep.gltf";
	Buffer payload(document.size(), MemTag::Asset);
	std::memcpy(payload.data, document.data(), document.size());
	ASSERT_TRUE(FileSystem::WriteFile(Path(deep.generic_string()), payload));

	ModelParseResult parsed = ParseGLTF(Path(deep.generic_string()));
	ASSERT_TRUE(parsed.success) << parsed.error;
	ASSERT_EQ(parsed.nodes.size(), static_cast<size_t>(DEPTH));

	Vector<u32> nodeDepth{GetMemoryResource(MemTag::Asset)};
	nodeDepth.resize(parsed.nodes.size(), 0);
	u32 deepest = 0;
	for (size_t i = 0; i < parsed.nodes.size(); ++i) {
		ASSERT_LT(parsed.nodes[i].parent, static_cast<i32>(i)) << "node " << i;
		if (parsed.nodes[i].parent >= 0) {
			nodeDepth[i] = nodeDepth[static_cast<size_t>(parsed.nodes[i].parent)] + 1;
		}
		deepest = std::max(deepest, nodeDepth[i]);
	}
	EXPECT_EQ(deepest, static_cast<u32>(DEPTH - 1));
	EXPECT_EQ(parsed.nodes.back().mesh, 0);
}

TEST_F(GraphicsImportFixture, ParseDispatchesByExtension) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ASSERT_TRUE(StageFixture("cube.glb"));

	ModelParseResult glTF = ParseModel(Path((_root / "hierarchy.gltf").generic_string()));
	ASSERT_TRUE(glTF.success) << glTF.error;
	EXPECT_EQ(glTF.nodes.size(), 5u);
	EXPECT_EQ(glTF.meshes.size(), 2u);

	ModelParseResult glb = ParseModel(Path((_root / "cube.glb").generic_string()));
	ASSERT_TRUE(glb.success) << glb.error;
	ASSERT_EQ(glb.meshes.size(), 1u);
	EXPECT_EQ(glb.meshes[0].vertices.size(), 24u);

	std::filesystem::path fbx = RepoDir("External/Submodules/ufbx/data/maya_cube_7500_binary.fbx");
	ASSERT_FALSE(fbx.empty());
	ModelParseResult parsedFBX = ParseModel(Path(fbx.generic_string()));
	ASSERT_TRUE(parsedFBX.success) << parsedFBX.error;
	ASSERT_EQ(parsedFBX.meshes.size(), 1u);
	EXPECT_EQ(parsedFBX.meshes[0].vertices.size(), 24u);

	ASSERT_TRUE(StageFixture("checker.png"));
	ModelParseResult notAModel = ParseModel(Path((_root / "checker.png").generic_string()));
	EXPECT_FALSE(notAModel.success);
	EXPECT_NE(notAModel.error.find(".png"), str::npos) << notAModel.error;
}

TEST_F(GraphicsImportFixture, MeshDescriptorImportsOneMeshOfModel) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	Guid model = ImportModel("project://hierarchy.gltf");
	ASSERT_TRUE(model.IsValid());
	ASSERT_TRUE(StageMeshDescriptor(_root / "hierarchy.gltf.meshes" / "0_Body.slmesh", model, 0, "Body"));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://hierarchy.gltf.meshes/0_Body.slmesh"));
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->GetAssetTypeId(), StringHash("MeshAsset"));
	MeshAsset* mesh = static_cast<MeshAsset*>(loaded);

	EXPECT_EQ(mesh->vertices.size(), 6u);
	EXPECT_EQ(mesh->indices.size(), 6u);
	ASSERT_EQ(mesh->submeshes.size(), 2u);

	EXPECT_NEAR(mesh->vertices[1].position.x, -1.0f, 1e-5f);
	EXPECT_NEAR(mesh->vertices[1].position.y, 0.0f, 1e-5f);
	EXPECT_NEAR(mesh->vertices[1].position.z, 0.0f, 1e-5f);

	EXPECT_TRUE(Vec3::Approximately(mesh->bounds.min, Vec3(-1.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(Vec3::Approximately(mesh->bounds.max, Vec3(0.0f, 1.0f, 1.0f)));

	EXPECT_EQ(mesh->submeshes[0].materialSlot, 0u);
	EXPECT_EQ(mesh->submeshes[1].materialSlot, 1u);

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, SubMeshesCarrySeparateBounds) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	Guid model = ImportModel("project://hierarchy.gltf");
	ASSERT_TRUE(model.IsValid());
	ASSERT_TRUE(StageMeshDescriptor(_root / "hierarchy.gltf.meshes" / "0_Body.slmesh", model, 0, "Body"));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://hierarchy.gltf.meshes/0_Body.slmesh"));
	ASSERT_NE(loaded, nullptr);
	MeshAsset* mesh = static_cast<MeshAsset*>(loaded);
	ASSERT_EQ(mesh->submeshes.size(), 2u);

	EXPECT_TRUE(Vec3::Approximately(mesh->submeshes[0].bounds.min, Vec3(-1.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(Vec3::Approximately(mesh->submeshes[0].bounds.max, Vec3(0.0f, 1.0f, 0.0f)));
	EXPECT_TRUE(Vec3::Approximately(mesh->submeshes[1].bounds.min, Vec3(-1.0f, 0.0f, 1.0f)));
	EXPECT_TRUE(Vec3::Approximately(mesh->submeshes[1].bounds.max, Vec3(0.0f, 1.0f, 1.0f)));

	AABB united = AABB::Empty();
	for (const SubMesh& submesh : mesh->submeshes) {
		AABB tight = AABB::Empty();
		for (u32 i = submesh.firstIndex; i < submesh.firstIndex + submesh.indexCount; ++i) {
			ASSERT_LT(mesh->indices[i], mesh->vertices.size());
			tight.Encapsulate(mesh->vertices[mesh->indices[i]].position);
		}
		EXPECT_TRUE(submesh.bounds.Contains(tight));
		united.Encapsulate(submesh.bounds);
	}
	EXPECT_TRUE(Vec3::Approximately(united.min, mesh->bounds.min));
	EXPECT_TRUE(Vec3::Approximately(united.max, mesh->bounds.max));

	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, MeshBindsMaterialsPerSlot) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	Guid model = ImportModel("project://hierarchy.gltf");
	ASSERT_TRUE(model.IsValid());

	Path source("project://hierarchy.gltf");
	Guid matA = ProductLookup::Find(source, "Materials/MatA");
	Guid matC = ProductLookup::Find(source, "Materials/MatC");
	ASSERT_TRUE(matA.IsValid());
	ASSERT_TRUE(matC.IsValid());
	EXPECT_TRUE(ProductLookup::Find(source, "Materials/MatUnused").IsValid());

	Asset* loaded = ProductLookup::Load(source, "Meshes/Body");
	ASSERT_NE(loaded, nullptr);
	MeshAsset* mesh = static_cast<MeshAsset*>(loaded);

	ASSERT_EQ(mesh->defaultMaterials.size(), 2u);
	EXPECT_EQ(mesh->defaultMaterials[0], matC);
	EXPECT_EQ(mesh->defaultMaterials[1], matA);

	Asset* slot0 = AssetDatabase::LoadAssetByGuid(mesh->defaultMaterials[0]);
	ASSERT_NE(slot0, nullptr);
	EXPECT_TRUE(Color::Approximately(static_cast<MaterialAsset*>(slot0)->GetColor(StringHash("baseColor")), Color(0.0f, 0.0f, 1.0f, 1.0f)));
	AssetDatabase::UnloadAsset(slot0);

	Asset* visorAsset = ProductLookup::Load(source, "Meshes/Visor");
	ASSERT_NE(visorAsset, nullptr);
	MeshAsset* visor = static_cast<MeshAsset*>(visorAsset);
	ASSERT_EQ(visor->defaultMaterials.size(), 1u);
	EXPECT_EQ(visor->defaultMaterials[0], matC);

	AssetDatabase::UnloadAsset(visorAsset);
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, MeshKeepsIndexOnNameDrift) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	Guid model = ImportModel("project://hierarchy.gltf");
	ASSERT_TRUE(model.IsValid());
	ASSERT_TRUE(StageMeshDescriptor(_root / "hierarchy.gltf.meshes" / "1_Visor.slmesh", model, 1, "SomethingElse"));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://hierarchy.gltf.meshes/1_Visor.slmesh"));
	ASSERT_NE(loaded, nullptr);
	MeshAsset* mesh = static_cast<MeshAsset*>(loaded);
	EXPECT_EQ(mesh->vertices.size(), 3u);
	EXPECT_EQ(mesh->submeshes.size(), 1u);
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, AMalformedMeshDescriptorIsRejected) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	Guid model = ImportModel("project://hierarchy.gltf");
	ASSERT_TRUE(model.IsValid());

	std::filesystem::path descriptor = _root / "hierarchy.gltf.meshes" / "0_Body.slmesh";
	Path virtualPath("project://hierarchy.gltf.meshes/0_Body.slmesh");
	std::error_code ec;
	std::filesystem::create_directories(descriptor.parent_path(), ec);

	auto reload = [&virtualPath]() {
		AssetDatabase::CollectGarbage();
		return AssetDatabase::LoadAsset(virtualPath);
	};

	ASSERT_TRUE(FileSystem::WriteTextFile(Path(descriptor.generic_string()), "not json {{{"));
	EXPECT_EQ(reload(), nullptr);

	ASSERT_TRUE(FileSystem::WriteTextFile(Path(descriptor.generic_string()), "{\n\t\"mesh\": 0\n}\n"));
	EXPECT_EQ(reload(), nullptr);

	ASSERT_TRUE(StageMeshDescriptor(descriptor, Guid{0xDEAD, 0xBEEF}, 0, "Body"));
	EXPECT_EQ(reload(), nullptr);

	ASSERT_TRUE(StageMeshDescriptor(descriptor, model, 7, "Body"));
	EXPECT_EQ(reload(), nullptr);

	ASSERT_TRUE(StageMeshDescriptor(descriptor, model, 0, "Body"));
	Asset* loaded = reload();
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(static_cast<MeshAsset*>(loaded)->vertices.size(), 6u);
	AssetDatabase::UnloadAsset(loaded);
}

TEST_F(GraphicsImportFixture, ModelPrefabRowPerNode) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	PrefabAsset* prefab = LoadModelPrefab("project://hierarchy.gltf");
	ASSERT_NE(prefab, nullptr);
	const SceneDocument* fragment = prefab->GetFragment();
	ASSERT_NE(fragment, nullptr);

	EXPECT_TRUE(fragment->isPrefab);
	EXPECT_FALSE(fragment->basePrefab.IsValid());
	ASSERT_EQ(fragment->rows.size(), 5u);

	constexpr u64 ROOT_ID = 2, bodyId = 4, muzzleId = 6, shearedId = 8, visorCopyId = 10;
	EXPECT_EQ(fragment->rows[0].id, ROOT_ID);
	EXPECT_EQ(fragment->rows[0].parent, 0u);
	EXPECT_EQ(fragment->rows[1].id, bodyId);
	EXPECT_EQ(fragment->rows[1].parent, ROOT_ID);
	EXPECT_EQ(fragment->rows[2].id, muzzleId);
	EXPECT_EQ(fragment->rows[2].parent, ROOT_ID);
	EXPECT_EQ(fragment->rows[3].id, shearedId);
	EXPECT_EQ(fragment->rows[3].parent, ROOT_ID);
	EXPECT_EQ(fragment->rows[4].id, visorCopyId);
	EXPECT_EQ(fragment->rows[4].parent, ROOT_ID);
	EXPECT_EQ(fragment->nextLocalId, 12u);

	SceneObject* root = ReadRow<SceneObject>(fragment->rows[0].fields);
	EXPECT_EQ(root->name, "hierarchy");
	SceneObject* muzzle = ReadRow<SceneObject>(fragment->rows[2].fields);
	EXPECT_EQ(muzzle->name, "Muzzle");
	TypeRegistry::DestroyObjectInstance(root);
	TypeRegistry::DestroyObjectInstance(muzzle);

	AssetDatabase::UnloadAsset(prefab);
}

TEST_F(GraphicsImportFixture, ModelPrefabCarriesEachNodesLocalTRS) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	PrefabAsset* prefab = LoadModelPrefab("project://hierarchy.gltf");
	ASSERT_NE(prefab, nullptr);
	const SceneDocument* fragment = prefab->GetFragment();
	ASSERT_NE(fragment, nullptr);

	SceneObject* root = ReadRow<SceneObject>(FindRow(*fragment, 2)->fields);
	EXPECT_TRUE(Vec3::Approximately(root->GetLocalPosition(), Vec3(-1.0f, 2.0f, 3.0f)));
	EXPECT_TRUE(Vec3::Approximately(root->GetLocalScale(), Vec3(1.0f, 1.0f, 1.0f)));

	SceneObject* body = ReadRow<SceneObject>(FindRow(*fragment, 4)->fields);
	EXPECT_TRUE(Vec3::Approximately(body->GetLocalPosition(), Vec3(0.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(Vec3::Approximately(body->GetLocalScale(), Vec3(2.0f, 2.0f, 2.0f)));

	SceneObject* muzzle = ReadRow<SceneObject>(FindRow(*fragment, 6)->fields);
	EXPECT_TRUE(Vec3::Approximately(muzzle->GetLocalPosition(), Vec3(0.0f, 1.0f, 0.0f)));
	EXPECT_TRUE(FindRow(*fragment, 6)->components.empty());

	SceneObject* visorCopy = ReadRow<SceneObject>(FindRow(*fragment, 10)->fields);
	EXPECT_TRUE(Vec3::Approximately(visorCopy->GetLocalPosition(), Vec3(0.0f, 0.0f, 5.0f)));

	TypeRegistry::DestroyObjectInstance(root);
	TypeRegistry::DestroyObjectInstance(body);
	TypeRegistry::DestroyObjectInstance(muzzle);
	TypeRegistry::DestroyObjectInstance(visorCopy);
	AssetDatabase::UnloadAsset(prefab);
}

TEST_F(GraphicsImportFixture, ModelPrefabMeshPerMesh) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	PrefabAsset* prefab = LoadModelPrefab("project://hierarchy.gltf");
	ASSERT_NE(prefab, nullptr);
	const SceneDocument* fragment = prefab->GetFragment();
	ASSERT_NE(fragment, nullptr);

	Path source("project://hierarchy.gltf");
	EXPECT_FALSE(std::filesystem::exists(_root / "hierarchy.gltf.meshes"));
	EXPECT_TRUE(ProductLookup::Find(source, "Meshes/Body").IsValid());
	EXPECT_TRUE(ProductLookup::Find(source, "Meshes/Visor").IsValid());

	Asset* bodyMesh = ProductLookup::Load(source, "Meshes/Body");
	Asset* visorMesh = ProductLookup::Load(source, "Meshes/Visor");
	ASSERT_NE(bodyMesh, nullptr);
	ASSERT_NE(visorMesh, nullptr);

	EXPECT_TRUE(FindRow(*fragment, 2)->components.empty());
	EXPECT_TRUE(FindRow(*fragment, 6)->components.empty());
	ASSERT_EQ(FindRow(*fragment, 4)->components.size(), 1u);
	EXPECT_EQ(FindRow(*fragment, 4)->components[0].id, 5u);
	EXPECT_EQ(FindRow(*fragment, 4)->components[0].type, MeshRenderer::GetReflectType()->name);

	MeshRenderer* body = ReadRow<MeshRenderer>(FindRow(*fragment, 4)->components[0].fields);
	ASSERT_TRUE(body->GetMesh().IsValid());
	EXPECT_EQ(body->GetMesh().Get(), bodyMesh);

	ASSERT_EQ(FindRow(*fragment, 8)->components.size(), 1u);
	ASSERT_EQ(FindRow(*fragment, 10)->components.size(), 1u);
	MeshRenderer* sheared = ReadRow<MeshRenderer>(FindRow(*fragment, 8)->components[0].fields);
	MeshRenderer* visorCopy = ReadRow<MeshRenderer>(FindRow(*fragment, 10)->components[0].fields);
	EXPECT_EQ(sheared->GetMesh().Get(), visorMesh);
	EXPECT_EQ(visorCopy->GetMesh().Get(), visorMesh);

	TypeRegistry::DestroyObjectInstance(body);
	TypeRegistry::DestroyObjectInstance(sheared);
	TypeRegistry::DestroyObjectInstance(visorCopy);
	AssetDatabase::UnloadAsset(bodyMesh);
	AssetDatabase::UnloadAsset(visorMesh);
	AssetDatabase::UnloadAsset(prefab);
}

TEST_F(GraphicsImportFixture, ModelPrefabPreBindsSlots) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	PrefabAsset* prefab = LoadModelPrefab("project://hierarchy.gltf");
	ASSERT_NE(prefab, nullptr);
	const SceneDocument* fragment = prefab->GetFragment();
	ASSERT_NE(fragment, nullptr);

	Asset* matA = ProductLookup::Load(Path("project://hierarchy.gltf"), "Materials/MatA");
	Asset* matC = ProductLookup::Load(Path("project://hierarchy.gltf"), "Materials/MatC");
	ASSERT_NE(matA, nullptr);
	ASSERT_NE(matC, nullptr);

	MeshRenderer* body = ReadRow<MeshRenderer>(FindRow(*fragment, 4)->components[0].fields);
	ASSERT_EQ(body->GetMaterialCount(), 2u);
	EXPECT_EQ(body->GetMaterial(static_cast<u32>(0)).Get(), matC);
	EXPECT_EQ(body->GetMaterial(static_cast<u32>(1)).Get(), matA);

	MeshRenderer* visorCopy = ReadRow<MeshRenderer>(FindRow(*fragment, 10)->components[0].fields);
	ASSERT_EQ(visorCopy->GetMaterialCount(), 1u);
	EXPECT_EQ(visorCopy->GetMaterial(static_cast<u32>(0)).Get(), matC);

	TypeRegistry::DestroyObjectInstance(body);
	TypeRegistry::DestroyObjectInstance(visorCopy);
	AssetDatabase::UnloadAsset(matA);
	AssetDatabase::UnloadAsset(matC);
	AssetDatabase::UnloadAsset(prefab);
}

TEST_F(GraphicsImportFixture, ModelWithSeveralRootsGetsSynthesizedRoot) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	ASSERT_TRUE(ImportModel("project://hierarchy.gltf").IsValid());

	ModelParseResult parsed;
	parsed.success = true;
	parsed.nodes.push_back(ModelNode{.name = "LeftHand"});
	parsed.nodes.push_back(ModelNode{.name = "RightHand"});

	PrefabAsset* prefab = BuildModelPrefab(parsed, Path("project://hierarchy.gltf"), {}, BuildModelRig(parsed));
	ASSERT_NE(prefab, nullptr);
	const SceneDocument* fragment = prefab->GetFragment();
	ASSERT_NE(fragment, nullptr);

	ASSERT_EQ(fragment->rows.size(), 3u);
	EXPECT_EQ(fragment->rows[0].id, 1u);
	EXPECT_EQ(fragment->rows[0].parent, 0u);
	EXPECT_EQ(fragment->rows[1].id, 2u);
	EXPECT_EQ(fragment->rows[1].parent, 1u);
	EXPECT_EQ(fragment->rows[2].id, 4u);
	EXPECT_EQ(fragment->rows[2].parent, 1u);

	SceneObject* root = ReadRow<SceneObject>(fragment->rows[0].fields);
	EXPECT_EQ(root->name, "hierarchy");
	SceneObject* left = ReadRow<SceneObject>(fragment->rows[1].fields);
	EXPECT_EQ(left->name, "LeftHand");

	TypeRegistry::DestroyObjectInstance(root);
	TypeRegistry::DestroyObjectInstance(left);
	Delete(prefab);
}

TEST_F(GraphicsImportFixture, ModelPrefabInstantiatesOneObjectPerNode) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));

	SceneSystem::Initialize();
	{
		Guid model = ImportModel("project://hierarchy.gltf");
		ASSERT_TRUE(model.IsValid());

		Handle<Scene> scene = SceneSystem::CreateScene("ModelDrop");
		Handle<SceneObject> plain = SceneSystem::CreateObject("Plain", scene);
		Guid authored = SceneSystem::SavePrefab(plain, Path("project://plain.prefab"));
		ASSERT_TRUE(authored.IsValid());
		Asset* authoredAsset = AssetDatabase::LoadAssetByGuid(authored);
		ASSERT_NE(authoredAsset, nullptr);
		EXPECT_EQ(static_cast<PrefabAsset*>(authoredAsset)->GetKind(), PrefabKind::Regular);
		AssetDatabase::UnloadAsset(authoredAsset);

		Handle<SceneObject> root = SceneSystem::Instantiate(model, scene);
		ASSERT_TRUE(root.IsValid());
		EXPECT_EQ(root->name, "hierarchy");
		ASSERT_EQ(root->GetChildCount(), 4u);

		Handle<SceneObject> body = root->GetChild(0);
		Handle<SceneObject> muzzle = root->GetChild(1);
		Handle<SceneObject> visorCopy = root->GetChild(3);
		EXPECT_EQ(body->name, "Body");
		EXPECT_EQ(muzzle->name, "Muzzle");
		EXPECT_EQ(visorCopy->name, "VisorCopy");

		EXPECT_TRUE(Vec3::Approximately(root->GetLocalPosition(), Vec3(-1.0f, 2.0f, 3.0f)));
		EXPECT_TRUE(Vec3::Approximately(body->GetLocalScale(), Vec3(2.0f, 2.0f, 2.0f)));
		EXPECT_TRUE(Vec3::Approximately(muzzle->GetLocalPosition(), Vec3(0.0f, 1.0f, 0.0f)));

		EXPECT_EQ(muzzle->GetComponent<MeshRenderer>().Get(), nullptr);

		Handle<MeshRenderer> renderer = body->GetComponent<MeshRenderer>();
		ASSERT_TRUE(renderer.IsValid());
		EXPECT_TRUE(renderer->GetMesh().IsValid());
		ASSERT_EQ(renderer->GetMaterialCount(), 2u);
		EXPECT_TRUE(renderer->GetMaterial(static_cast<u32>(0)).IsValid());
		EXPECT_TRUE(renderer->GetMaterial(static_cast<u32>(1)).IsValid());

		EXPECT_EQ(renderer->GetMesh().Get()->bounds.min.x, -1.0f);
		EXPECT_EQ(visorCopy->GetComponent<MeshRenderer>()->GetMesh().Get(),
				  root->GetChild(2)->GetComponent<MeshRenderer>()->GetMesh().Get());

		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();
	}
	SceneSystem::Terminate();
}

TEST_F(GraphicsImportFixture, ModelPrefabRefusesWriteBack) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));
	std::filesystem::path modelFile = _root / "hierarchy.gltf";
	u64 bytesBefore = std::filesystem::file_size(modelFile);

	SceneSystem::Initialize();
	{
		Guid model = ImportModel("project://hierarchy.gltf");
		ASSERT_TRUE(model.IsValid());
		Handle<Scene> scene = SceneSystem::CreateScene("ModelRefusals");
		Handle<SceneObject> root = SceneSystem::Instantiate(model, scene);
		ASSERT_TRUE(root.IsValid());

		Handle<SceneObject> body = root->GetChild(0);
		body->SetLocalPosition(Vec3(9.0f, 0.0f, 0.0f));
		EXPECT_TRUE(Vec3::Approximately(body->GetLocalPosition(), Vec3(9.0f, 0.0f, 0.0f)));

		EXPECT_FALSE(SceneSystem::ApplyInstanceOverridesToBase(root));
		EXPECT_TRUE(Vec3::Approximately(body->GetLocalPosition(), Vec3(9.0f, 0.0f, 0.0f)));

		Handle<SceneObject> second = SceneSystem::Instantiate(model, scene);
		ASSERT_TRUE(second.IsValid());
		EXPECT_TRUE(Vec3::Approximately(second->GetChild(0)->GetLocalPosition(), Vec3(0.0f, 0.0f, 0.0f)));

		Handle<SceneObject> detached = SceneSystem::InstantiateDetached(model);
		ASSERT_TRUE(detached.IsValid());
		detached->GetChild(0)->SetLocalPosition(Vec3(-4.0f, 0.0f, 0.0f));
		EXPECT_FALSE(SceneSystem::ApplyDetachedToPrefab(detached, model));
		SceneSystem::DestroyDetached(detached);

		Handle<SceneObject> loose = SceneSystem::CreateObject("Loose", scene);
		EXPECT_FALSE(SceneSystem::SavePrefab(loose, Path("project://hierarchy.gltf")).IsValid());
		EXPECT_EQ(std::filesystem::file_size(modelFile), bytesBefore);
		EXPECT_TRUE(SceneSystem::SavePrefab(loose, Path("project://loose.prefab")).IsValid());

		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();
	}
	SceneSystem::Terminate();
}

TEST_F(GraphicsImportFixture, ReimportKeepsRowIds) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));

	PrefabAsset* before = LoadModelPrefab("project://hierarchy.gltf");
	ASSERT_NE(before, nullptr);
	const SceneDocument* firstFragment = before->GetFragment();
	ASSERT_NE(firstFragment, nullptr);
	Vector<u64> idsBefore{GetMemoryResource(MemTag::Scene)};
	for (const SceneDocumentRow& row : firstFragment->rows) {
		idsBefore.push_back(row.id);
	}
	ASSERT_FALSE(idsBefore.empty());
	const Guid guidBefore = before->assetId;
	const size_t rowsBefore = idsBefore.size();
	AssetDatabase::UnloadAsset(before);
	AssetDatabase::CollectGarbage();

	ASSERT_TRUE(AppendNodeToStagedModel("project://hierarchy.gltf", "Antenna"));

	PrefabAsset* after = LoadModelPrefab("project://hierarchy.gltf");
	ASSERT_NE(after, nullptr);
	const SceneDocument* second = after->GetFragment();
	ASSERT_NE(second, nullptr);

	EXPECT_EQ(after->assetId, guidBefore);
	for (u64 id : idsBefore) {
		EXPECT_NE(FindRow(*second, id), nullptr) << "row " << id;
	}
	for (size_t node = 0; node < 5; ++node) {
		EXPECT_NE(FindRow(*second, 2 * static_cast<u64>(node) + 2), nullptr) << "node " << node << ' ' << 2 * node + 2;
	}
	for (size_t node : {size_t(1), size_t(3), size_t(4)}) {
		const SceneDocumentRow* row = FindRow(*second, 2 * static_cast<u64>(node) + 2);
		ASSERT_NE(row, nullptr);
		ASSERT_EQ(row->components.size(), 1u) << "node " << node;
		EXPECT_EQ(row->components[0].id, 2 * static_cast<u64>(node) + 3);
	}
	const SceneDocumentRow* muzzle = FindRow(*second, 2 * 2u + 2);
	ASSERT_NE(muzzle, nullptr);
	EXPECT_TRUE(muzzle->components.empty());
	EXPECT_EQ(second->rows.size(), rowsBefore + 1);
	ASSERT_NE(FindRow(*second, 12u), nullptr);
	AssetDatabase::UnloadAsset(after);
}

TEST_F(GraphicsImportFixture, DiskEditReachesSceneKeepsOverrides) {
	ASSERT_TRUE(StageFixture("hierarchy.gltf"));

	SceneSystem::Initialize();
	{
		Guid model = ImportModel("project://hierarchy.gltf");
		ASSERT_TRUE(model.IsValid());

		Handle<Scene> scene = SceneSystem::CreateScene("Level");
		Handle<SceneObject> edited = SceneSystem::Instantiate(model, scene);
		Handle<SceneObject> pristine = SceneSystem::Instantiate(model, scene);
		ASSERT_TRUE(edited.IsValid() && pristine.IsValid());
		ASSERT_EQ(edited->GetChildCount(), 4u);

		edited->FindChildByName("Muzzle")->SetLocalPosition(Vec3(9.0f, 9.0f, 9.0f));
		edited->FindChildByName("Body")->name = "Hull";

		ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://level.scene")));
		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();

		ASSERT_TRUE(AppendNodeToStagedModel("project://hierarchy.gltf", "Antenna"));
		ASSERT_TRUE(ReimportUnreferencedModel("project://hierarchy.gltf"));

		Handle<Scene> reopened = SceneSystem::LoadScene(Path("project://level.scene"));
		ASSERT_TRUE(reopened.IsValid());
		ASSERT_EQ(reopened->GetRootCount(), 2u);
		Handle<SceneObject> editedAgain = reopened->GetRoot(0);
		Handle<SceneObject> pristineAgain = reopened->GetRoot(1);
		ASSERT_TRUE(editedAgain.IsValid() && pristineAgain.IsValid());

		EXPECT_EQ(editedAgain->GetChildCount(), 5u);
		EXPECT_TRUE(editedAgain->FindChildByName("Antenna").IsValid());
		EXPECT_TRUE(pristineAgain->FindChildByName("Antenna").IsValid());

		Handle<SceneObject> muzzle = editedAgain->FindChildByName("Muzzle");
		ASSERT_TRUE(muzzle.IsValid());
		EXPECT_TRUE(Vec3::Approximately(muzzle->GetLocalPosition(), Vec3(9.0f, 9.0f, 9.0f)));
		EXPECT_TRUE(editedAgain->FindChildByName("Hull").IsValid());
		EXPECT_EQ(editedAgain->FindChildByName("Body").Get(), nullptr);

		EXPECT_TRUE(pristineAgain->FindChildByName("Body").IsValid());
		EXPECT_TRUE(Vec3::Approximately(pristineAgain->FindChildByName("Muzzle")->GetLocalPosition(),
										Vec3(0.0f, 1.0f, 0.0f)));

		SceneSystem::UnloadScene(reopened);
		SceneSystem::EndFrame();
	}
	SceneSystem::Terminate();
}

TEST_F(GraphicsImportFixture, MeshlessDocumentImportsTracks) {
	ASSERT_TRUE(StageFixture("animated.gltf"));

	std::filesystem::path source = _root / "animated.gltf";
	str document = FileSystem::ReadTextFile(Path(source.generic_string()), MemTag::Asset);
	ASSERT_FALSE(document.empty());

	constexpr std::string_view MeshRef = "\"mesh\": 0,";
	size_t at = document.find(MeshRef);
	ASSERT_NE(at, str::npos);
	document = document.substr(0, at) + document.substr(at + MeshRef.size());

	std::filesystem::path meshless = _root / "meshless.gltf";
	Buffer payload(document.size(), MemTag::Asset);
	std::memcpy(payload.data, document.data(), document.size());
	ASSERT_TRUE(FileSystem::WriteFile(Path(meshless.generic_string()), payload));

	ModelParseOptions options;
	options.tracks = true;
	ModelParseResult parsed = ParseGLTF(Path(meshless.generic_string()), options);

	ASSERT_TRUE(parsed.success) << parsed.error;
	for (const ModelNode& node : parsed.nodes) {
		EXPECT_LT(node.mesh, 0) << "node '" << node.name;
	}
	EXPECT_EQ(parsed.nodes.size(), 4u);
	ASSERT_EQ(parsed.takes.size(), 3u);
	EXPECT_FALSE(parsed.tracks.empty());
}

TEST_F(GraphicsImportFixture, EmptyDocumentRefusedByName) {
	constexpr std::string_view EMPTY = "{\"asset\":{\"version\":\"2.0\"}}";
	std::filesystem::path empty = _root / "empty.gltf";
	Buffer payload(EMPTY.size(), MemTag::Asset);
	std::memcpy(payload.data, EMPTY.data(), EMPTY.size());
	ASSERT_TRUE(FileSystem::WriteFile(Path(empty.generic_string()), payload));

	ModelParseResult parsed = ParseGLTF(Path(empty.generic_string()));
	EXPECT_FALSE(parsed.success);
	EXPECT_EQ(parsed.error, "the file has no nodes/geometry");
}

TEST_F(GraphicsImportFixture, LoadingMaterialCompilesVariantKeywordsName) {
	ASSERT_TRUE(FileSystem::WriteTextFile(
		Path("project://Keyworded.hlsl"),
		str("#pragma shalltear_keyword FEATURE_A\n"
			"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };\n"
			"struct VSOutput { float4 positionCS : SV_Position; };\n"
			"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
			"\tVSOutput output;\n"
			"\toutput.positionCS = float4(float(vertexId), 0, 0, 1);\n"
			"\treturn output;\n"
			"}\n"
			"float4 PSMain(VSOutput input) : SV_Target {\n"
			"\tfloat3 tint = float3(1, 0, 0);\n"
			"#ifdef FEATURE_A\n"
			"\ttint = float3(0, 1, 0);\n"
			"#endif\n"
			"\treturn float4(tint * baseColor.rgb, 1);\n"
			"}\n")));
	Guid shaderGuid = AssetDatabase::EnsureAssetIdentity(Path("project://Keyworded.hlsl"));
	ASSERT_TRUE(shaderGuid.IsValid());
	{
		Asset* probe = AssetDatabase::LoadAssetByGuid(shaderGuid);
		ASSERT_NE(nullptr, probe);
		bool compiled = !static_cast<ShaderAsset*>(probe)->variants.empty();
		AssetDatabase::UnloadAsset(probe);
		if (!compiled) {
			GTEST_SKIP() << "dxcompiler.dll unavailable (run Scripts/fetch_prebuilt.py)";
		}
	}

	ASSERT_TRUE(FileSystem::WriteTextFile(
		Path("project://Keyworded.mat"),
		std::format("{{\n\t\"shader\": \"{}\",\n\t\"floats\": {{}},\n\t\"colors\": {{}},\n"
					"\t\"textures\": {{}},\n\t\"keywords\": [ \"FEATURE_A\" ]\n}}\n",
					shaderGuid.ToString())));

	Asset* loaded = AssetDatabase::LoadAsset(Path("project://Keyworded.mat"));
	ASSERT_NE(nullptr, loaded);
	MaterialAsset* material = static_cast<MaterialAsset*>(loaded);
	ASSERT_EQ(1u, material->keywords.size());

	ShaderAsset* shader = material->shader.Get();
	ASSERT_NE(nullptr, shader);
	EXPECT_EQ(2u, shader->variants.size());
	EXPECT_EQ(1u, shader->FindVariant(material->keywordHash));

	AssetDatabase::UnloadAsset(loaded);
}
