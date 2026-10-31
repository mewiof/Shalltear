#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef CreateDirectory
#undef GetFileSize

using namespace Shalltear;

namespace {

	std::filesystem::path FindEngineAssets() {
		std::filesystem::path dir = std::filesystem::current_path();
		for (u32 i = 0; i < 8; ++i) {
			std::filesystem::path candidate = dir / "EngineAssets";
			if (std::filesystem::exists(candidate / "Shaders" / "Lit.hlsl")) {
				return candidate;
			}
			if (!dir.has_parent_path() || dir.parent_path() == dir) {
				break;
			}
			dir = dir.parent_path();
		}
		return {};
	}

	const void* ImageOf(const void* address) {
		HMODULE image = nullptr;
		::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							 static_cast<const char*>(address), &image);
		return image;
	}

	class PrimitiveAssetFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _engineMounted = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_prim_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));

			std::filesystem::path engineAssets = FindEngineAssets();
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_engineMounted = true;
			}
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			if (_engineMounted) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}
	};

} // namespace

TEST(PrimitiveMeshTest, ShapeNamesRoundTrip) {
	for (u8 i = 0; i < static_cast<u8>(PrimitiveShape::Count); ++i) {
		PrimitiveShape shape = static_cast<PrimitiveShape>(i);
		std::string_view name = PrimitiveShapeName(shape);
		ASSERT_FALSE(name.empty()) << "shape " << static_cast<u32>(i);

		PrimitiveShape parsed{};
		ASSERT_TRUE(ParsePrimitiveShape(name, parsed));
		EXPECT_EQ(parsed, shape);
	}

	PrimitiveShape ignored{};
	EXPECT_FALSE(ParsePrimitiveShape("Dodecahedron", ignored));
	EXPECT_FALSE(ParsePrimitiveShape("cube", ignored));
}

TEST(PrimitiveMeshTest, ShapePathsPointAtShippedDescriptors) {
	EXPECT_EQ(GetPrimitiveMeshPath(PrimitiveShape::Cube), "engine://Meshes/Cube.primitive");
	EXPECT_EQ(GetPrimitiveMeshPath(PrimitiveShape::Capsule), "engine://Meshes/Capsule.primitive");
	EXPECT_TRUE(GetPrimitiveMeshPath(PrimitiveShape::Count).empty());
}

TEST(PrimitiveMeshTest, EveryShapeGeneratesWellFormedGeometry) {
	for (u8 i = 0; i < static_cast<u8>(PrimitiveShape::Count); ++i) {
		PrimitiveShape shape = static_cast<PrimitiveShape>(i);
		SCOPED_TRACE(str(PrimitiveShapeName(shape)));

		MeshAsset mesh;
		ASSERT_TRUE(BuildPrimitiveMesh(shape, mesh));

		ASSERT_FALSE(mesh.vertices.empty());
		ASSERT_FALSE(mesh.indices.empty());
		EXPECT_EQ(mesh.indices.size() % 3, 0u);
		ASSERT_EQ(mesh.submeshes.size(), 1u);
		EXPECT_EQ(mesh.submeshes[0].firstIndex, 0u);
		EXPECT_EQ(mesh.submeshes[0].indexCount, static_cast<u32>(mesh.indices.size()));
		EXPECT_EQ(mesh.submeshes[0].materialSlot, 0u);

		for (u32 index : mesh.indices) {
			ASSERT_LT(index, mesh.vertices.size());
		}

		for (const MeshVertex& vertex : mesh.vertices) {
			EXPECT_NEAR(vertex.normal.Length(), 1.0f, 1.0e-4f);
			EXPECT_NEAR(vertex.tangent.Length(), 1.0f, 1.0e-4f);
			EXPECT_LT(std::fabs(Vec3::Dot(vertex.normal, vertex.tangent)), 0.999f);
		}

		for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
			const Vec3& a = mesh.vertices[mesh.indices[t]].position;
			const Vec3& b = mesh.vertices[mesh.indices[t + 1]].position;
			const Vec3& c = mesh.vertices[mesh.indices[t + 2]].position;
			f32 area = Vec3::Cross(b - a, c - a).Length() * 0.5f;
			EXPECT_GT(area, 1.0e-7f) << t;
		}
	}
}

TEST(PrimitiveMeshTest, EveryTriangleWindsWithNormal) {
	for (u8 i = 0; i < static_cast<u8>(PrimitiveShape::Count); ++i) {
		PrimitiveShape shape = static_cast<PrimitiveShape>(i);
		SCOPED_TRACE(str(PrimitiveShapeName(shape)));

		MeshAsset mesh;
		ASSERT_TRUE(BuildPrimitiveMesh(shape, mesh));

		for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
			const MeshVertex& a = mesh.vertices[mesh.indices[t]];
			const MeshVertex& b = mesh.vertices[mesh.indices[t + 1]];
			const MeshVertex& c = mesh.vertices[mesh.indices[t + 2]];
			Vec3 wound = Vec3::Cross(b.position - a.position, c.position - a.position);
			Vec3 authored = a.normal + b.normal + c.normal;
			EXPECT_GT(Vec3::Dot(wound, authored), 0.0f) << "triangle " << t / 3 << ' ' << a.normal.x << ", " << a.normal.y << ", " << a.normal.z << ")";
		}
	}
}

TEST(PrimitiveMeshTest, EveryVertexBasisIsConsistentlyHanded) {
	for (u8 i = 0; i < static_cast<u8>(PrimitiveShape::Count); ++i) {
		PrimitiveShape shape = static_cast<PrimitiveShape>(i);
		SCOPED_TRACE(str(PrimitiveShapeName(shape)));

		MeshAsset mesh;
		ASSERT_TRUE(BuildPrimitiveMesh(shape, mesh));
		for (const MeshVertex& vertex : mesh.vertices) {
			Vec3 bitangent = Vec3::Cross(vertex.normal, vertex.tangent);
			Vec3 back = Vec3::Cross(vertex.tangent, bitangent);
			EXPECT_GT(Vec3::Dot(back, vertex.normal), 0.0f);
			EXPECT_FLOAT_EQ(vertex.tangentSign, 1.0f);
		}
	}
}

TEST(PrimitiveMeshTest, BoundsMatchGeneratedPositions) {
	for (u8 i = 0; i < static_cast<u8>(PrimitiveShape::Count); ++i) {
		PrimitiveShape shape = static_cast<PrimitiveShape>(i);
		SCOPED_TRACE(str(PrimitiveShapeName(shape)));

		MeshAsset mesh;
		ASSERT_TRUE(BuildPrimitiveMesh(shape, mesh));
		ASSERT_TRUE(mesh.bounds.IsValid());

		AABB expected = AABB::Empty();
		for (const MeshVertex& vertex : mesh.vertices) {
			expected.Encapsulate(vertex.position);
		}
		EXPECT_NEAR(mesh.bounds.min.x, expected.min.x, 1.0e-5f);
		EXPECT_NEAR(mesh.bounds.min.y, expected.min.y, 1.0e-5f);
		EXPECT_NEAR(mesh.bounds.min.z, expected.min.z, 1.0e-5f);
		EXPECT_NEAR(mesh.bounds.max.x, expected.max.x, 1.0e-5f);
		EXPECT_NEAR(mesh.bounds.max.y, expected.max.y, 1.0e-5f);
		EXPECT_NEAR(mesh.bounds.max.z, expected.max.z, 1.0e-5f);

		ASSERT_EQ(mesh.submeshes.size(), 1u);
		EXPECT_TRUE(Vec3::Approximately(mesh.submeshes[0].bounds.min, mesh.bounds.min));
		EXPECT_TRUE(Vec3::Approximately(mesh.submeshes[0].bounds.max, mesh.bounds.max));
	}
}

TEST(PrimitiveMeshTest, ShapesCentredAtUsualSize) {
	struct Expected {
		PrimitiveShape shape;
		Vec3 size;
	};
	const Expected sizes[] = {
		{PrimitiveShape::Cube, {1.0f, 1.0f, 1.0f}},
		{PrimitiveShape::Sphere, {1.0f, 1.0f, 1.0f}},
		{PrimitiveShape::Plane, {10.0f, 0.0f, 10.0f}},
		{PrimitiveShape::Quad, {1.0f, 1.0f, 0.0f}},
		{PrimitiveShape::Cylinder, {1.0f, 2.0f, 1.0f}},
		{PrimitiveShape::Capsule, {1.0f, 2.0f, 1.0f}},
	};
	static_assert(sizeof(sizes) / sizeof(sizes[0]) == static_cast<size_t>(PrimitiveShape::Count));

	for (const Expected& expected : sizes) {
		SCOPED_TRACE(str(PrimitiveShapeName(expected.shape)));
		MeshAsset mesh;
		ASSERT_TRUE(BuildPrimitiveMesh(expected.shape, mesh));

		EXPECT_NEAR(mesh.bounds.Size().x, expected.size.x, 1.0e-4f);
		EXPECT_NEAR(mesh.bounds.Size().y, expected.size.y, 1.0e-4f);
		EXPECT_NEAR(mesh.bounds.Size().z, expected.size.z, 1.0e-4f);
		EXPECT_NEAR(mesh.bounds.Center().Length(), 0.0f, 1.0e-5f);
	}

	MeshAsset sphere;
	ASSERT_TRUE(BuildPrimitiveMesh(PrimitiveShape::Sphere, sphere));
	for (const MeshVertex& vertex : sphere.vertices) {
		EXPECT_NEAR(vertex.position.Length(), 0.5f, 1.0e-4f);
	}
}

TEST(PrimitiveMeshTest, CapsuleIsTwoHemispheres) {
	constexpr f32 RADIUS = 0.5f;
	constexpr f32 HALF_MID = 0.5f;

	MeshAsset capsule;
	ASSERT_TRUE(BuildPrimitiveMesh(PrimitiveShape::Capsule, capsule));

	bool sawTopCap = false;
	bool sawBody = false;
	bool sawBottomCap = false;
	for (const MeshVertex& vertex : capsule.vertices) {
		const Vec3& p = vertex.position;
		if (p.y > HALF_MID) {
			EXPECT_NEAR((p - Vec3{0.0f, HALF_MID, 0.0f}).Length(), RADIUS, 1.0e-4f);
			sawTopCap = true;
		} else if (p.y < -HALF_MID) {
			EXPECT_NEAR((p - Vec3{0.0f, -HALF_MID, 0.0f}).Length(), RADIUS, 1.0e-4f);
			sawBottomCap = true;
		} else {
			EXPECT_NEAR(std::sqrt(p.x * p.x + p.z * p.z), RADIUS, 1.0e-4f);
			sawBody = true;
		}
	}
	EXPECT_TRUE(sawTopCap);
	EXPECT_TRUE(sawBody);
	EXPECT_TRUE(sawBottomCap);
}

TEST(PrimitiveMeshTest, CapsuleBodyEndsAtFullRadius) {
	constexpr f32 RADIUS = 0.5f;
	constexpr f32 HALF_MID = 0.5f;

	MeshAsset capsule;
	ASSERT_TRUE(BuildPrimitiveMesh(PrimitiveShape::Capsule, capsule));

	u32 atTop = 0;
	u32 atBottom = 0;
	for (const MeshVertex& vertex : capsule.vertices) {
		const Vec3& p = vertex.position;
		if (std::fabs(std::sqrt(p.x * p.x + p.z * p.z) - RADIUS) > 1.0e-4f) {
			continue;
		}
		if (std::fabs(p.y - HALF_MID) <= 1.0e-4f) {
			++atTop;
		} else if (std::fabs(p.y + HALF_MID) <= 1.0e-4f) {
			++atBottom;
		}
	}

	EXPECT_EQ(atTop, 33u);
	EXPECT_EQ(atBottom, 33u);
}

TEST(PrimitiveMeshTest, BuildReplacesPriorContents) {
	MeshAsset mesh;
	ASSERT_TRUE(BuildPrimitiveMesh(PrimitiveShape::Sphere, mesh));
	size_t sphereVertices = mesh.vertices.size();

	ASSERT_TRUE(BuildPrimitiveMesh(PrimitiveShape::Cube, mesh));
	EXPECT_EQ(mesh.vertices.size(), 24u);
	EXPECT_NE(mesh.vertices.size(), sphereVertices);
	EXPECT_EQ(mesh.submeshes.size(), 1u);
}

TEST(PrimitiveMeshTest, OutOfRangeShapeIsRefused) {
	MeshAsset mesh;
	EXPECT_FALSE(BuildPrimitiveMesh(PrimitiveShape::Count, mesh));
}

TEST_F(PrimitiveAssetFixture, ShippedDescriptorsImportForEveryShape) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	for (u8 i = 0; i < static_cast<u8>(PrimitiveShape::Count); ++i) {
		PrimitiveShape shape = static_cast<PrimitiveShape>(i);
		str path = GetPrimitiveMeshPath(shape);
		SCOPED_TRACE(path);

		Asset* asset = AssetDatabase::LoadAsset(Path(path));
		ASSERT_NE(asset, nullptr);
		EXPECT_EQ(asset->GetAssetTypeId(), StringHash("MeshAsset"));

		MeshAsset* mesh = static_cast<MeshAsset*>(asset);
		EXPECT_FALSE(mesh->vertices.empty());
		EXPECT_FALSE(mesh->indices.empty());
		AssetDatabase::UnloadAsset(asset);
	}
}

TEST_F(PrimitiveAssetFixture, PackageImportedAssetSharesCoresVTable) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Asset* imported = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Sphere)));
	ASSERT_NE(imported, nullptr);

	const void* coreAnchor = reinterpret_cast<const void*>(&ObjectRegistry::Register);
	const void* assetVTable = *reinterpret_cast<void* const*>(imported);

	EXPECT_EQ(ImageOf(assetVTable), ImageOf(coreAnchor));

	AssetDatabase::UnloadAsset(imported);
}

TEST_F(PrimitiveAssetFixture, ShippedDescriptorsCarryTheirCommittedGuids) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Asset* first = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	ASSERT_NE(first, nullptr);
	Guid guid = first->assetId;
	EXPECT_TRUE(guid.IsValid());
	AssetDatabase::UnloadAsset(first);

	Asset* again = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	ASSERT_NE(again, nullptr);
	EXPECT_EQ(again->assetId, guid);
	AssetDatabase::UnloadAsset(again);
}

TEST_F(PrimitiveAssetFixture, PrimitivesBindTheDefaultMaterial) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Asset* asset = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	ASSERT_NE(asset, nullptr);
	MeshAsset* mesh = static_cast<MeshAsset*>(asset);

	ASSERT_EQ(mesh->defaultMaterials.size(), 1u);
	EXPECT_TRUE(mesh->defaultMaterials[0].IsValid());
	EXPECT_EQ(AssetDatabase::LookupPath(mesh->defaultMaterials[0]).ToString(), "engine://Materials/Default.mat");
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(PrimitiveAssetFixture, EngineAssetGuidsResolveAfterARestart) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Asset* capsule = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Capsule)));
	ASSERT_NE(capsule, nullptr);
	Guid guid = capsule->assetId;
	AssetDatabase::UnloadAsset(capsule);

	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	AssetDatabase::ScanProjectDirectory();

	EXPECT_EQ(AssetDatabase::LookupPath(guid).ToString(), GetPrimitiveMeshPath(PrimitiveShape::Capsule));
	Asset* reloaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(reloaded, nullptr);
	AssetDatabase::UnloadAsset(reloaded);
}

TEST_F(PrimitiveAssetFixture, ImportersShareAssetType) {
	IAssetImporter* descriptor = AssetImporterRegistry::FindByExtension(".slmesh");
	IAssetImporter* primitive = AssetImporterRegistry::FindByExtension(".primitive");
	ASSERT_NE(descriptor, nullptr);
	ASSERT_NE(primitive, nullptr);
	EXPECT_NE(descriptor, primitive);
	EXPECT_EQ(descriptor->GetAssetType(), StringHash("MeshAsset"));
	EXPECT_EQ(primitive->GetAssetType(), StringHash("MeshAsset"));

	IAssetImporter* model = AssetImporterRegistry::FindByExtension(".gltf");
	ASSERT_NE(model, nullptr);
	EXPECT_NE(model, descriptor);
	EXPECT_EQ(model->GetAssetType(), StringHash("PrefabAsset"));
}

TEST_F(PrimitiveAssetFixture, UnknownShapeNameFailsCleanly) {
	Path path("project://Bad.primitive");
	FileSystem::WriteTextFile(FileSystem::Resolve(path), "{\"shape\": \"Torus\"}");

	EXPECT_EQ(AssetDatabase::LoadAsset(path), nullptr);
}

#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/UndoSystem.h"

namespace {

	class PrimitiveObjectFixture : public PrimitiveAssetFixture {
	protected:
		void SetUp() override {
			PrimitiveAssetFixture::SetUp();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			UndoSystem::Initialize();
			_scene = SceneSystem::CreateScene("PrimitiveFixture");
			SceneSystem::SetPrimaryScene(_scene);
		}
		void TearDown() override {
			UndoSystem::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
			PrimitiveAssetFixture::TearDown();
		}
		Handle<Scene> _scene;
	};

} // namespace

TEST_F(PrimitiveObjectFixture, CreatePrimitiveObjectBindsMeshAndMaterial) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Handle<SceneObject> object = EditorSceneOps::CreatePrimitiveObject(PrimitiveShape::Cube, _scene);
	ASSERT_TRUE(object.IsValid());
	EXPECT_EQ(object.Get()->name, "Cube");

	Handle<MeshRenderer> renderer = object.Get()->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	EXPECT_TRUE(renderer.Get()->GetMesh().IsValid());
	EXPECT_TRUE(renderer.Get()->GetMaterial(0).IsValid());
}

TEST_F(PrimitiveObjectFixture, CreatePrimitiveObjectIsOneUndoGroup) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Handle<SceneObject> object = EditorSceneOps::CreatePrimitiveObject(PrimitiveShape::Sphere, _scene);
	ASSERT_TRUE(object.IsValid());

	EXPECT_TRUE(UndoSystem::CanUndo());
	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_FALSE(object.IsValid());
	EXPECT_FALSE(UndoSystem::CanUndo());
}

namespace {

	Handle<SceneObject> FindRootNamed(Handle<Scene> scene, std::string_view name) {
		Scene* live = scene.Get();
		if (!live) {
			return {};
		}
		for (u32 i = 0; i < live->GetRootCount(); ++i) {
			Handle<SceneObject> root = live->GetRoot(i);
			if (root.IsValid() && root.Get()->name == name) {
				return root;
			}
		}
		return {};
	}

} // namespace

TEST_F(PrimitiveObjectFixture, RedonePrimitiveKeepsMesh) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Handle<SceneObject> object = EditorSceneOps::CreatePrimitiveObject(PrimitiveShape::Cube, _scene);
	ASSERT_TRUE(object.IsValid());
	ASSERT_TRUE(object.Get()->GetComponent<MeshRenderer>().IsValid());

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	ASSERT_FALSE(object.IsValid());

	ASSERT_TRUE(UndoSystem::CanRedo());
	UndoSystem::Redo();
	SceneSystem::EndFrame();

	Handle<SceneObject> redone = FindRootNamed(_scene, "Cube");
	ASSERT_TRUE(redone.IsValid());

	Handle<MeshRenderer> renderer = redone.Get()->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	EXPECT_TRUE(renderer.Get()->GetMesh().IsValid());
	EXPECT_TRUE(renderer.Get()->GetMaterial(0).IsValid());
}

TEST_F(PrimitiveObjectFixture, RedoneCameraAndLightKeepComponents) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
	EditorSceneOps::BuildCreateMenu(_scene, {}, items);

	for (const char* label : {"Camera", "Light"}) {
		SCOPED_TRACE(label);
		const UI::Widgets::MenuItem* entry = nullptr;
		for (const UI::Widgets::MenuItem& item : items) {
			if (item.label == label) {
				entry = &item;
			}
		}
		ASSERT_NE(entry, nullptr);
		const UI::Widgets::MenuItem& invocable = entry->children.empty() ? *entry : entry->children[0];
		ASSERT_TRUE(static_cast<bool>(invocable.onSelect));
		invocable.onSelect();

		Handle<SceneObject> made = FindRootNamed(_scene, label);
		ASSERT_TRUE(made.IsValid());
		const u32 before = made.Get()->GetComponentCount();
		ASSERT_GT(before, 0u);

		UndoSystem::Undo();
		SceneSystem::EndFrame();
		UndoSystem::Redo();
		SceneSystem::EndFrame();

		Handle<SceneObject> redone = FindRootNamed(_scene, label);
		ASSERT_TRUE(redone.IsValid());
		EXPECT_EQ(redone.Get()->GetComponentCount(), before);
	}
}

TEST_F(PrimitiveObjectFixture, EveryShapeProducesARenderableObject) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	for (u8 i = 0; i < static_cast<u8>(PrimitiveShape::Count); ++i) {
		PrimitiveShape shape = static_cast<PrimitiveShape>(i);
		SCOPED_TRACE(str(PrimitiveShapeName(shape)));
		Handle<SceneObject> object = EditorSceneOps::CreatePrimitiveObject(shape, _scene);
		ASSERT_TRUE(object.IsValid());
		Handle<MeshRenderer> renderer = object.Get()->GetComponent<MeshRenderer>();
		ASSERT_TRUE(renderer.IsValid());
		EXPECT_TRUE(renderer.Get()->GetMesh().IsValid());
	}
}

TEST_F(PrimitiveObjectFixture, CreatedPrimitiveStillHasMeshAfterReopen) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	Handle<SceneObject> object = EditorSceneOps::CreatePrimitiveObject(PrimitiveShape::Capsule, _scene);
	ASSERT_TRUE(object.IsValid());
	Guid meshId = object.Get()->GetComponent<MeshRenderer>().Get()->GetMesh().Get()->assetId;
	Guid materialId = object.Get()->GetComponent<MeshRenderer>().Get()->GetMaterial(0).Get()->assetId;

	ASSERT_TRUE(SceneSystem::SaveScene(_scene, Path("project://reopen.scene")));

	SceneSystem::UnloadAllScenes();
	SceneSystem::EndFrame();
	SceneSystem::Terminate();
	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	AssetDatabase::ScanProjectDirectory();
	SceneSystem::Initialize();

	Handle<Scene> reopened = SceneSystem::LoadScene(Path("project://reopen.scene"));
	ASSERT_TRUE(reopened.IsValid());
	ASSERT_EQ(reopened->GetRootCount(), 1u);

	Handle<SceneObject> capsule = reopened->GetRoot(0);
	EXPECT_EQ(capsule->name, "Capsule");
	Handle<MeshRenderer> renderer = capsule->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());

	ASSERT_TRUE(renderer.Get()->GetMesh().IsValid());
	EXPECT_EQ(renderer.Get()->GetMesh().Get()->assetId, meshId);
	ASSERT_TRUE(renderer.Get()->GetMaterial(0).IsValid());
	EXPECT_EQ(renderer.Get()->GetMaterial(0).Get()->assetId, materialId);
}

TEST_F(PrimitiveObjectFixture, CreatePrimitiveObjectHonorsAnExplicitParent) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	Handle<SceneObject> parent = SceneSystem::CreateObject("Parent", _scene);
	ASSERT_TRUE(parent.IsValid());

	Handle<SceneObject> child = EditorSceneOps::CreatePrimitiveObject(PrimitiveShape::Cube, _scene, parent);
	ASSERT_TRUE(child.IsValid());
	EXPECT_EQ(child.Get()->GetParent().GetInstanceId(), parent.GetInstanceId());
	ASSERT_EQ(parent.Get()->GetChildCount(), 1u);

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_FALSE(child.IsValid());
	EXPECT_TRUE(parent.IsValid());
}

TEST_F(PrimitiveObjectFixture, ParentlessPrimitiveAtRoot) {
	if (!_engineMounted) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	Handle<SceneObject> object = EditorSceneOps::CreatePrimitiveObject(PrimitiveShape::Cube, _scene);
	ASSERT_TRUE(object.IsValid());
	EXPECT_FALSE(object.Get()->GetParent().IsValid());
}
