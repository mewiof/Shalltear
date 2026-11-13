#include "TestsPCH.h"

#include "Checkout.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	namespace fs = std::filesystem;

	class MaterialInstanceFixture : public ::testing::Test {
	protected:
		fs::path _root;
		bool _mountedEngine = false;
		Handle<Scene> _scene;
		MaterialAsset* _shared = nullptr;
		Asset* _cube = nullptr;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() / ("shalltear_instance_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root);
			SettingsPaths paths;
			paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			Settings::Initialize(paths);
			const fs::path engineAssets = Checkout::Find("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			_scene = SceneSystem::CreateScene("Instances");
			if (!_mountedEngine) {
				return;
			}
			_shared = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat")));
			_cube = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
		}

		void TearDown() override {
			SceneSystem::Terminate();
			if (_shared) {
				AssetDatabase::UnloadAsset(_shared);
			}
			if (_cube) {
				AssetDatabase::UnloadAsset(_cube);
			}
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			Settings::Terminate();
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		bool Ready() const { return _shared && _shared->shader.IsValid() && _cube; }

		Handle<MeshRenderer> AddCube(const char* name, const Vec3& position) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			object->SetLocalPosition(position);
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(_cube)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(_shared));
			return renderer;
		}
	};

} // anonymous namespace

TEST_F(MaterialInstanceFixture, InstanceOwnsParamsSharesShader) {
	if (!Ready()) {
		GTEST_SKIP() << "default material unavailable (dxcompiler.dll missing?)";
	}
	const Handle<MaterialAsset> shared(_shared);
	ShaderAsset* shader = _shared->shader.Get();
	const int shaderRefs = shader->GetRefCount();

	Handle<MaterialAsset> instance = MaterialAsset::CreateInstance(shared);
	ASSERT_TRUE(instance.IsValid());
	EXPECT_EQ(instance->shader.Get(), shader);
	EXPECT_EQ(shader->GetRefCount(), shaderRefs + 1);
	EXPECT_FLOAT_EQ(instance->GetFloat("roughness"_hash), _shared->GetFloat("roughness"_hash));

	instance->SetColor("baseColor", Color(0.0f, 1.0f, 0.0f, 1.0f));
	EXPECT_EQ(_shared->GetColor("baseColor"_hash), Color::White());
	EXPECT_EQ(instance->GetColor("baseColor"_hash), Color(0.0f, 1.0f, 0.0f, 1.0f));

	MaterialAsset::DestroyInstance(instance);
	EXPECT_FALSE(instance.IsValid());
	EXPECT_EQ(shader->GetRefCount(), shaderRefs);

	MaterialAsset::DestroyInstance(shared);
	EXPECT_TRUE(shared.IsValid());
	EXPECT_FALSE(MaterialAsset::CreateInstance({}).IsValid());
}

TEST_F(MaterialInstanceFixture, RendererInstanceLivesWithSlot) {
	if (!Ready()) {
		GTEST_SKIP() << "default material unavailable (dxcompiler.dll missing?)";
	}
	const Handle<MaterialAsset> shared(_shared);
	const int shaderRefs = _shared->shader->GetRefCount();
	Handle<MeshRenderer> renderer = AddCube("Cube", Vec3::Zero());
	EXPECT_EQ(renderer->GetRenderedMaterial(0), shared);

	const Handle<MaterialAsset> first = renderer->GetMaterialInstance(0);
	ASSERT_TRUE(first.IsValid());
	EXPECT_NE(first, shared);
	EXPECT_EQ(renderer->GetMaterialInstance(0), first);
	EXPECT_EQ(renderer->GetRenderedMaterial(0), first);
	EXPECT_EQ(renderer->GetMaterial(0), shared);

	const str saved = TextSerializer::Serialize(renderer.Get(), MeshRenderer::GetReflectType());
	EXPECT_NE(saved.find(_shared->assetId.ToString()), str::npos) << saved;
	EXPECT_EQ(saved.find("(Instance)"), str::npos);

	renderer->SetMaterial(0, shared);
	EXPECT_FALSE(first.IsValid());
	EXPECT_EQ(renderer->GetRenderedMaterial(0), shared);

	const Handle<MaterialAsset> second = renderer->GetMaterialInstance(0);
	ASSERT_TRUE(second.IsValid());
	EXPECT_EQ(_shared->shader->GetRefCount(), shaderRefs + 1);
	SceneSystem::Destroy(renderer->GetOwner());
	SceneSystem::EndFrame();
	EXPECT_FALSE(second.IsValid());
	EXPECT_EQ(_shared->shader->GetRefCount(), shaderRefs);
}

TEST_F(MaterialInstanceFixture, InstanceDrawsOverriddenColor) {
	if (!Ready() || !RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device or default material";
	}
	RenderSystem::SetSceneCamerasRenderToWindows(false);
	AddCube("Left", Vec3(-1.2f, 0.0f, 0.0f));
	Handle<MeshRenderer> right = AddCube("Right", Vec3(1.2f, 0.0f, 0.0f));
	Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", _scene);
	sun->SetLocalRotation(Quat::LookRotation(Vec3(0.2f, -0.4f, 1.0f).Normalized()));
	sun->AddComponent<Light>()->SetIntensity(2.0f);
	Handle<SceneObject> eye = SceneSystem::CreateObject("Eye", _scene);
	eye->SetLocalPosition(Vec3(0.0f, 0.0f, -6.0f));
	Handle<Camera> camera = eye->AddComponent<Camera>();
	camera->SetBackgroundType(BackgroundType::SolidColor);
	camera->SetClearColor(Color::Black());

	right->GetMaterialInstance(0)->SetColor("baseColor", Color(1.0f, 0.0f, 0.0f, 1.0f));
	const ViewTargetId target = RenderSystem::CreateViewTarget({128, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	const Handle<Camera> cameras[] = {camera};
	for (int frame = 0; frame < 3; ++frame) {
		SceneSystem::EndFrame();
		RenderSystem::SubmitCameras(target, cameras);
		RenderSystem::RenderAll();
	}
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	RenderSystem::DestroyViewTarget(target);

	auto brightest = [&](i32 fromX, i32 toX) {
		Vec3 best;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = fromX; x < toX; ++x) {
				const size_t at = (static_cast<size_t>(y) * size.x + x) * 4;
				const Vec3 texel(pixels[at], pixels[at + 1], pixels[at + 2]);
				if (texel.x + texel.y + texel.z > best.x + best.y + best.z) {
					best = texel;
				}
			}
		}
		return best;
	};
	const Vec3 left = brightest(0, size.x / 2);
	const Vec3 lit = brightest(size.x / 2, size.x);
	ASSERT_GT(left.x, 40.0f);
	EXPECT_NEAR(left.y, left.x, 8.0f);
	ASSERT_GT(lit.x, 40.0f);
	EXPECT_LT(lit.y, lit.x * 0.25f);
}
