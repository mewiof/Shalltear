#include "TestsPCH.h"

#include "ProductLookup.h"

#include "Graphics/Skinning.h"
#include "ModelContributorsInternal.h"
#include "ModelImport.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkinAsset.h"
#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Window.h"

#include <atomic>
#include <chrono>
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

	SkeletonAsset* MakeTwoBoneSkeleton() {
		SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
		skeleton->boneNames.push_back("Root");
		skeleton->parents.push_back(-1);
		skeleton->restPose.push_back(BoneTransform{});

		skeleton->boneNames.push_back("Tip");
		skeleton->parents.push_back(0);
		BoneTransform tip;
		tip.position = Vec3(0.0f, 1.0f, 0.0f);
		skeleton->restPose.push_back(tip);
		return skeleton;
	}

	SkinAsset* MakeTwoBoneSkin() {
		SkinAsset* skin = New<SkinAsset>(MemTag::Asset);
		skin->jointToBone.push_back(0);
		skin->jointToBone.push_back(1);
		skin->inverseBind.push_back(Mat4::Identity());
		skin->inverseBind.push_back(Mat4::TRS(Vec3(0.0f, -1.0f, 0.0f), Quat::Identity(), Vec3(1.0f, 1.0f, 1.0f)));
		return skin;
	}

	class SkinningFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_skin_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			FileSystem::Unmount("engine");
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
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

TEST_F(SkinningFixture, LocalPoseMovesExposedBone) {
	Handle<Scene> scene = SceneSystem::CreateScene("Pose");
	Handle<SceneObject> rig = SceneSystem::CreateObject("Rig", scene);
	Handle<SceneObject> boneObject = SceneSystem::CreateObject("Tip", scene, rig);

	SkeletonAsset* skeleton = MakeTwoBoneSkeleton();
	Handle<Skeleton> component = rig->AddComponent<Skeleton>();
	ASSERT_TRUE(component.IsValid());
	component->SetSkeleton(Handle<SkeletonAsset>(skeleton));
	component->AddExposedBone(1, boneObject);

	component->UpdatePose();
	EXPECT_TRUE(Vec3::Approximately(boneObject->GetLocalPosition(), Vec3(0.0f, 0.0f, 0.0f)));

	BoneTransform posed;
	posed.position = Vec3(3.0f, 0.0f, 0.0f);
	component->SetBoneLocalPose(1, posed);
	component->UpdatePose();
	EXPECT_TRUE(Vec3::Approximately(boneObject->GetLocalPosition(), Vec3(3.0f, 0.0f, 0.0f)));

	boneObject->SetLocalPosition(Vec3(-9.0f, 0.0f, 0.0f));
	EXPECT_TRUE(Vec3::Approximately(component->GetLocalPose()[1].position, Vec3(3.0f, 0.0f, 0.0f)));

	posed.position = Vec3(4.0f, 0.0f, 0.0f);
	component->SetBoneLocalPose(1, posed);
	component->UpdatePose();
	EXPECT_TRUE(Vec3::Approximately(boneObject->GetLocalPosition(), Vec3(4.0f, 0.0f, 0.0f)));

	Delete(skeleton);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(SkinningFixture, NarrowedListKeepsBoneObjects) {
	Handle<Scene> scene = SceneSystem::CreateScene("Expose");
	Handle<SceneObject> rig = SceneSystem::CreateObject("Rig", scene);
	Handle<SceneObject> first = SceneSystem::CreateObject("A", scene, rig);
	Handle<SceneObject> second = SceneSystem::CreateObject("B", scene, rig);

	SkeletonAsset* skeleton = MakeTwoBoneSkeleton();
	Handle<Skeleton> component = rig->AddComponent<Skeleton>();
	component->SetSkeleton(Handle<SkeletonAsset>(skeleton));
	component->AddExposedBone(0, first);
	component->AddExposedBone(1, second);
	component->UpdatePose();

	const u64 firstId = first.GetInstanceId();
	const u64 secondId = second.GetInstanceId();

	component->SetExposedBoneCount(1);
	BoneTransform moved;
	moved.position = Vec3(0.0f, 5.0f, 0.0f);
	component->SetBoneLocalPose(0, moved);
	component->UpdatePose();

	EXPECT_TRUE(first.IsValid());
	EXPECT_TRUE(second.IsValid());
	EXPECT_EQ(first.GetInstanceId(), firstId);
	EXPECT_EQ(second.GetInstanceId(), secondId);
	EXPECT_TRUE(Vec3::Approximately(first->GetLocalPosition(), Vec3(0.0f, 5.0f, 0.0f)));

	Delete(skeleton);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(SkinningFixture, ModelPoseComposesChildAgainstParent) {
	Handle<Scene> scene = SceneSystem::CreateScene("Compose");
	Handle<SceneObject> rig = SceneSystem::CreateObject("Rig", scene);
	SkeletonAsset* skeleton = MakeTwoBoneSkeleton();
	Handle<Skeleton> component = rig->AddComponent<Skeleton>();
	component->SetSkeleton(Handle<SkeletonAsset>(skeleton));
	component->UpdatePose();

	std::span<const Mat4> model = component->GetModelPose();
	ASSERT_EQ(model.size(), 2u);
	EXPECT_TRUE(Vec3::Approximately(model[1].TransformPoint(Vec3(0.0f, 0.0f, 0.0f)), Vec3(0.0f, 1.0f, 0.0f)));

	BoneTransform rootPosed;
	rootPosed.position = Vec3(10.0f, 0.0f, 0.0f);
	component->SetBoneLocalPose(0, rootPosed);
	component->UpdatePose();
	model = component->GetModelPose();
	EXPECT_TRUE(Vec3::Approximately(model[1].TransformPoint(Vec3(0.0f, 0.0f, 0.0f)), Vec3(10.0f, 1.0f, 0.0f)));

	Delete(skeleton);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(SkinningFixture, SkinPerBinding) {
	ASSERT_EQ(ModelContributors::Fingerprint(), 0u);
	ASSERT_TRUE(StageFixture("SimpleSkin.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
	ASSERT_NE(model, nullptr);
	AssetDatabase::UnloadAsset(model);

	const Path source("project://SimpleSkin.gltf");
	Guid skeletonGuid = ProductLookup::Find(source, "Skeletons/SimpleSkin");
	ASSERT_TRUE(skeletonGuid.IsValid());
	Guid skinGuid = ProductLookup::Find(source, "Skins/Skin0");
	ASSERT_TRUE(skinGuid.IsValid());

	auto* skeleton = static_cast<SkeletonAsset*>(AssetDatabase::LoadAssetByGuid(skeletonGuid));
	ASSERT_NE(skeleton, nullptr);
	ASSERT_EQ(skeleton->GetBoneCount(), 2u);
	EXPECT_EQ(skeleton->parents[0], -1);
	EXPECT_EQ(skeleton->parents[1], 0);
	for (size_t b = 0; b < skeleton->parents.size(); ++b) {
		EXPECT_LT(skeleton->parents[b], static_cast<i16>(b)) << "bone " << b;
	}
	EXPECT_TRUE(Vec3::Approximately(skeleton->restPose[1].position, Vec3(0.0f, 1.0f, 0.0f)));

	auto* skin = static_cast<SkinAsset*>(AssetDatabase::LoadAssetByGuid(skinGuid));
	ASSERT_NE(skin, nullptr);
	ASSERT_EQ(skin->GetJointCount(), 2u);
	EXPECT_EQ(skin->jointToBone[0], 0);
	EXPECT_EQ(skin->jointToBone[1], 1);
	ASSERT_EQ(skin->inverseBind.size(), 2u);
	EXPECT_EQ(skin->skeletonGuid, skeletonGuid);
	EXPECT_EQ(skin->skeleton.GetInstanceId(), skeleton->GetInstanceId());

	Mat4 skinMatrix = Mat4::TRS(skeleton->restPose[1].position, skeleton->restPose[1].rotation, skeleton->restPose[1].scale) * skin->inverseBind[1];
	EXPECT_TRUE(Vec3::Approximately(skinMatrix.TransformPoint(Vec3(0.25f, 1.5f, 0.0f)), Vec3(0.25f, 1.5f, 0.0f)));

	const size_t boneCount = skeleton->GetBoneCount();
	const str firstBoneName = skeleton->boneNames[0];
	AssetDatabase::UnloadAsset(skin);
	AssetDatabase::UnloadAsset(skeleton);
	AssetDatabase::CollectGarbage();

	auto* warmSkeleton = static_cast<SkeletonAsset*>(AssetDatabase::LoadAssetByGuid(skeletonGuid));
	ASSERT_NE(warmSkeleton, nullptr);
	EXPECT_EQ(warmSkeleton->GetBoneCount(), boneCount);
	ASSERT_FALSE(warmSkeleton->boneNames.empty());
	EXPECT_EQ(warmSkeleton->boneNames[0], firstBoneName);
	EXPECT_EQ(warmSkeleton->parents[1], 0);
	EXPECT_TRUE(Vec3::Approximately(warmSkeleton->restPose[1].position, Vec3(0.0f, 1.0f, 0.0f)));

	auto* warmSkin = static_cast<SkinAsset*>(AssetDatabase::LoadAssetByGuid(skinGuid));
	ASSERT_NE(warmSkin, nullptr);
	ASSERT_EQ(warmSkin->GetJointCount(), 2u);
	ASSERT_EQ(warmSkin->inverseBind.size(), 2u);
	EXPECT_EQ(warmSkin->skeletonGuid, skeletonGuid);
	EXPECT_TRUE(warmSkin->skeleton.IsValid());

	AssetDatabase::UnloadAsset(warmSkin);
	AssetDatabase::UnloadAsset(warmSkeleton);
}

TEST_F(SkinningFixture, MeshCarriesInfluenceStream) {
	ASSERT_TRUE(StageFixture("SimpleSkin.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
	ASSERT_NE(model, nullptr);
	AssetDatabase::UnloadAsset(model);

	Guid meshGuid;
	str meshKey;
	for (const str& key : ProductLookup::Keys(Path("project://SimpleSkin.gltf"))) {
		if (key.starts_with("Meshes/")) {
			meshKey = key;
			meshGuid = ProductLookup::Find(Path("project://SimpleSkin.gltf"), key);
			break;
		}
	}
	ASSERT_TRUE(meshGuid.IsValid());

	auto* mesh = static_cast<MeshAsset*>(AssetDatabase::LoadAssetByGuid(meshGuid));
	ASSERT_NE(mesh, nullptr);
	EXPECT_EQ(mesh->vertices.size(), 10u);
	ASSERT_EQ(mesh->skinInfluences.size(), mesh->vertices.size());

	f32 blended = 0.0f;
	for (const MeshSkinInfluence& influence : mesh->skinInfluences) {
		f32 sum = influence.weights[0] + influence.weights[1] + influence.weights[2] + influence.weights[3];
		EXPECT_NEAR(sum, 1.0f, 1e-4f);
		if (influence.weights[1] > 0.01f && influence.weights[0] > 0.01f) {
			++blended;
		}
	}
	EXPECT_GT(blended, 0.0f);

	AssetDatabase::UnloadAsset(mesh);
	AssetDatabase::CollectGarbage();
	auto* again = static_cast<MeshAsset*>(AssetDatabase::LoadAssetByGuid(meshGuid));
	ASSERT_NE(again, nullptr);
	EXPECT_EQ(again->skinInfluences.size(), again->vertices.size());
	AssetDatabase::UnloadAsset(again);
}

TEST_F(SkinningFixture, SkinnedNodeSharesSkeleton) {
	ASSERT_TRUE(StageFixture("SimpleSkin.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	Handle<Scene> scene = SceneSystem::CreateScene("Drop");
	Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, scene);
	ASSERT_TRUE(root.IsValid());

	Handle<SkinnedMeshRenderer> renderer;
	Handle<Skeleton> skeletonComponent = root->GetComponent<Skeleton>();
	auto walk = [&](auto&& self, const Handle<SceneObject>& object) -> void {
		if (Handle<SkinnedMeshRenderer> found = object->GetComponent<SkinnedMeshRenderer>(); found.IsValid()) {
			renderer = found;
		}
		for (size_t i = 0; i < object->GetChildCount(); ++i) {
			self(self, object->GetChild(i));
		}
	};
	walk(walk, root);

	ASSERT_TRUE(skeletonComponent.IsValid());
	ASSERT_TRUE(renderer.IsValid());
	EXPECT_FALSE(renderer->GetOwner()->GetComponent<MeshRenderer>().IsValid());
	EXPECT_EQ(renderer->GetSkeleton().GetInstanceId(), skeletonComponent.GetInstanceId());
	ASSERT_TRUE(renderer->GetSkin().IsValid());
	ASSERT_TRUE(renderer->GetMesh().IsValid());

	ASSERT_EQ(skeletonComponent->GetExposedBoneCount(), 2u);
	ASSERT_EQ(skeletonComponent->GetExposedBoneCount(), 2u);
	EXPECT_TRUE(skeletonComponent->GetExposedObject(static_cast<u32>(0)).IsValid());
	EXPECT_TRUE(skeletonComponent->GetExposedObject(static_cast<u32>(1)).IsValid());

	BoneTransform posed;
	posed.position = Vec3(0.0f, 7.0f, 0.0f);
	skeletonComponent->SetBoneLocalPose(1, posed);
	skeletonComponent->UpdatePose();
	EXPECT_TRUE(Vec3::Approximately(skeletonComponent->GetExposedObject(static_cast<u32>(1))->GetLocalPosition(), Vec3(0.0f, 7.0f, 0.0f)));

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

namespace {

	void CountHalves(const Vector<u8>& pixels, Vec2I size, u32& outTop, u32& outBottom) {
		outTop = 0;
		outBottom = 0;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
				if (pixels[index] > 8 || pixels[index + 1] > 8 || pixels[index + 2] > 8) {
					++(y < size.y / 2 ? outTop : outBottom);
				}
			}
		}
	}

	ViewDesc SkinnedView() {
		ViewDesc desc;
		desc.view = Mat4::LookAt(Vec3(0.0f, 1.0f, 4.0f), Vec3(0.0f, 1.0f, 0.0f));
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.cameraPosition = Vec3(0.0f, 1.0f, 4.0f);
		desc.clearColor = Color::Black();
		return desc;
	}

} // anonymous namespace

TEST_F(SkinningFixture, SkinnedMeshRendersBindPose) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_EQ(ModelContributors::Fingerprint(), 0u);
	ASSERT_TRUE(StageFixture("SimpleSkin.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	Handle<Scene> scene = SceneSystem::CreateScene("Render");
	Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, scene);
	ASSERT_TRUE(root.IsValid());
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc = SkinnedView();
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	u32 top = 0;
	u32 bottom = 0;
	CountHalves(pixels, size, top, bottom);
	if (top + bottom == 0) {
		GTEST_SKIP();
	}
	EXPECT_GT(top, 100u);
	EXPECT_GT(bottom, 100u);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(SkinningFixture, PosedBoneMovesPixels) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(StageFixture("SimpleSkin.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	Handle<Scene> scene = SceneSystem::CreateScene("Pose");
	Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, scene);
	ASSERT_TRUE(root.IsValid());
	Handle<Skeleton> skeleton = root->GetComponent<Skeleton>();
	ASSERT_TRUE(skeleton.IsValid());
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc = SkinnedView();

	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> bind{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, bind, size));
	u32 bindTop = 0;
	u32 bindBottom = 0;
	CountHalves(bind, size, bindTop, bindBottom);
	if (bindTop + bindBottom == 0) {
		GTEST_SKIP();
	}
	ASSERT_GT(bindTop, 100u);

	BoneTransform folded;
	folded.position = Vec3(0.0f, 6.0f, 0.0f);
	skeleton->SetBoneLocalPose(1, folded);
	skeleton->UpdatePose();

	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> posed{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, posed, size));
	u32 posedTop = 0;
	u32 posedBottom = 0;
	CountHalves(posed, size, posedTop, posedBottom);

	EXPECT_GT(posedTop, bindTop + 100u) << bindTop << ", posed " << posedTop;
	EXPECT_NEAR(static_cast<f32>(posedBottom), static_cast<f32>(bindBottom), 8.0f);
	EXPECT_NE(bind, posed);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(SkinningFixture, DeadSkeletonFallsBackToBindPose) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(StageFixture("SimpleSkin.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	Handle<Scene> scene = SceneSystem::CreateScene("Dying");
	Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, scene);
	ASSERT_TRUE(root.IsValid());
	Handle<Skeleton> skeleton = root->GetComponent<Skeleton>();
	ASSERT_TRUE(skeleton.IsValid());

	Handle<SkinnedMeshRenderer> renderer;
	auto walk = [&](auto&& self, const Handle<SceneObject>& object) -> void {
		if (Handle<SkinnedMeshRenderer> found = object->GetComponent<SkinnedMeshRenderer>(); found.IsValid()) {
			renderer = found;
		}
		for (size_t i = 0; i < object->GetChildCount(); ++i) {
			self(self, object->GetChild(i));
		}
	};
	walk(walk, root);
	ASSERT_TRUE(renderer.IsValid());
	SceneSystem::EndFrame();

	SceneSystem::Destroy(skeleton->GetOwner());
	SceneSystem::EndFrame();
	EXPECT_FALSE(skeleton.IsValid());

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc = SkinnedView();
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(SkinningFixture, MeshUnderTwoSkinsDiffers) {
	Handle<Scene> scene = SceneSystem::CreateScene("TwoSkins");
	Handle<SceneObject> rig = SceneSystem::CreateObject("Rig", scene);
	SkeletonAsset* skeleton = MakeTwoBoneSkeleton();
	Handle<Skeleton> component = rig->AddComponent<Skeleton>();
	component->SetSkeleton(Handle<SkeletonAsset>(skeleton));

	BoneTransform folded;
	folded.position = Vec3(0.0f, 1.0f, 0.0f);
	folded.rotation = Quat::AngleAxis(90.0f, Vec3(0.0f, 0.0f, 1.0f));
	component->SetBoneLocalPose(1, folded);
	component->UpdatePose();
	std::span<const Mat4> model = component->GetModelPose();

	SkinAsset* first = MakeTwoBoneSkin();
	SkinAsset* second = MakeTwoBoneSkin();
	second->inverseBind[1] = Mat4::TRS(Vec3(0.0f, -2.0f, 0.0f), Quat::Identity(), Vec3::One());

	Mat4 firstMatrices[2];
	Mat4 secondMatrices[2];
	GraphicsInternal::BuildSkinMatrices(*first, model, firstMatrices, 2);
	GraphicsInternal::BuildSkinMatrices(*second, model, secondMatrices, 2);

	const Vec3 probe(0.25f, 1.5f, 0.0f);
	Vec3 a = firstMatrices[1].TransformPoint(probe);
	Vec3 b = secondMatrices[1].TransformPoint(probe);
	EXPECT_FALSE(Vec3::Approximately(a, b));

	Delete(second);
	Delete(first);
	Delete(skeleton);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST(SkinningKernelTest, UnboundVertexKeepsPosition) {
	MeshVertex vertices[2]{};
	vertices[0].position = Vec3(1.0f, 0.0f, 0.0f);
	vertices[0].normal = Vec3(0.0f, 0.0f, 1.0f);
	vertices[1].position = Vec3(2.0f, 0.0f, 0.0f);
	vertices[1].normal = Vec3(0.0f, 0.0f, 1.0f);

	MeshSkinInfluence influences[2]{};
	influences[0].joints[0] = 0;
	influences[0].weights[0] = 1.0f;

	Mat4 matrices[1] = {Mat4::TRS(Vec3(10.0f, 0.0f, 0.0f), Quat::Identity(), Vec3::One())};

	MeshVertex out[2]{};
	GraphicsInternal::SkinVertices({vertices, 2}, {influences, 2}, {matrices, 1}, out);

	EXPECT_TRUE(Vec3::Approximately(out[0].position, Vec3(11.0f, 0.0f, 0.0f)));
	EXPECT_TRUE(Vec3::Approximately(out[1].position, Vec3(2.0f, 0.0f, 0.0f)));
}

TEST(SkinningKernelTest, OutOfRangeJointDropped) {
	MeshVertex vertices[1]{};
	vertices[0].position = Vec3(0.0f, 4.0f, 0.0f);
	vertices[0].normal = Vec3(0.0f, 0.0f, 1.0f);

	MeshSkinInfluence influences[1]{};
	influences[0].joints[0] = 0;
	influences[0].weights[0] = 0.5f;
	influences[0].joints[1] = 99;
	influences[0].weights[1] = 0.5f;

	Mat4 matrices[1] = {Mat4::TRS(Vec3(0.0f, 0.0f, 6.0f), Quat::Identity(), Vec3::One())};

	MeshVertex out[1]{};
	GraphicsInternal::SkinVertices({vertices, 1}, {influences, 1}, {matrices, 1}, out);
	EXPECT_TRUE(Vec3::Approximately(out[0].position, Vec3(0.0f, 4.0f, 6.0f)));
}

TEST(SkinningKernelTest, BonelessJointGetsIdentity) {
	SkinAsset skin;
	skin.jointToBone.push_back(-1);
	skin.jointToBone.push_back(0);
	skin.inverseBind.push_back(Mat4::TRS(Vec3(5.0f, 0.0f, 0.0f), Quat::Identity(), Vec3::One()));
	skin.inverseBind.push_back(Mat4::Identity());

	Mat4 pose[1] = {Mat4::Identity()};
	Mat4 matrices[2];
	GraphicsInternal::BuildSkinMatrices(skin, {pose, 1}, matrices, 2);

	EXPECT_TRUE(Vec3::Approximately(matrices[0].TransformPoint(Vec3(1.0f, 2.0f, 3.0f)), Vec3(1.0f, 2.0f, 3.0f)));
	EXPECT_TRUE(Vec3::Approximately(matrices[1].TransformPoint(Vec3(1.0f, 2.0f, 3.0f)), Vec3(1.0f, 2.0f, 3.0f)));
}

TEST_F(SkinningFixture, SkinnedDrawFollowsSkeletonNotObject) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(StageFixture("SimpleSkin.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	Handle<Scene> scene = SceneSystem::CreateScene("Space");
	Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, scene);
	ASSERT_TRUE(root.IsValid());
	Handle<SkinnedMeshRenderer> renderer;
	auto walk = [&](auto&& self, const Handle<SceneObject>& object) -> void {
		if (Handle<SkinnedMeshRenderer> found = object->GetComponent<SkinnedMeshRenderer>(); found.IsValid()) {
			renderer = found;
		}
		for (size_t i = 0; i < object->GetChildCount(); ++i) {
			self(self, object->GetChild(i));
		}
	};
	walk(walk, root);
	ASSERT_TRUE(renderer.IsValid());
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc = SkinnedView();
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> before{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, before, size));
	u32 beforeTop = 0;
	u32 beforeBottom = 0;
	CountHalves(before, size, beforeTop, beforeBottom);
	if (beforeTop + beforeBottom == 0) {
		GTEST_SKIP();
	}

	Handle<SceneObject> carrier = SceneSystem::CreateObject("Carrier", scene);
	carrier->SetLocalPosition(Vec3(2.0f, 0.0f, 0.0f));
	renderer->GetOwner()->SetParent(carrier, false);
	SceneSystem::EndFrame();

	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> after{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, after, size));
	EXPECT_EQ(before, after);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

namespace {

	MeshAsset* MakeTwoVertexMesh() {
		MeshAsset* mesh = New<MeshAsset>(MemTag::Asset);
		MeshVertex a;
		a.position = Vec3(0.0f, 0.0f, 0.0f);
		a.normal = Vec3(0.0f, 0.0f, 1.0f);
		MeshVertex b;
		b.position = Vec3(1.0f, 0.0f, 0.0f);
		b.normal = Vec3(0.0f, 0.0f, 1.0f);
		mesh->vertices.push_back(a);
		mesh->vertices.push_back(b);
		return mesh;
	}

	MeshBlendShapeFrame MakeFrame(f32 at, const Vec3& by, const Vec3& normalBy = Vec3{}) {
		MeshBlendShapeFrame frame;
		frame.weight = at;
		frame.vertices.push_back(0);
		frame.positionDeltas.push_back(by);
		if (!(normalBy == Vec3{})) {
			frame.normalDeltas.push_back(normalBy);
		}
		return frame;
	}

	Vec3 MorphedPosition(const MeshAsset& mesh, std::span<const f32> weights, size_t vertex) {
		Vector<MeshVertex> out{GetMemoryResource(MemTag::Asset)};
		out.resize(mesh.vertices.size());
		GraphicsInternal::ApplyBlendShapes(mesh, weights, out.data());
		return out[vertex].position;
	}

} // anonymous namespace

TEST(BlendShapeKernelTest, OneFrameScalesLinearlyThroughRestPose) {
	MeshAsset* mesh = MakeTwoVertexMesh();
	MeshBlendShape shape;
	shape.name = "Up";
	shape.frames.push_back(MakeFrame(100.0f, Vec3(0.0f, 4.0f, 0.0f)));
	mesh->blendShapes.push_back(std::move(shape));

	const f32 off[1] = {0.0f};
	const f32 half[1] = {50.0f};
	const f32 full[1] = {100.0f};
	const f32 past[1] = {200.0f};
	const f32 below[1] = {-50.0f};

	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, off, 0).y, 0.0f);
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, half, 0).y, 2.0f);
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, full, 0).y, 4.0f);
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, past, 0).y, 8.0f);
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, below, 0).y, -2.0f);

	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, full, 1).y, 0.0f);
	Delete(mesh);
}

TEST(BlendShapeKernelTest, SliderBetween2FramesLerpsBracketingPair) {
	MeshAsset* mesh = MakeTwoVertexMesh();
	MeshBlendShape shape;
	shape.name = "Wide";
	shape.frames.push_back(MakeFrame(50.0f, Vec3(0.0f, 10.0f, 0.0f)));
	shape.frames.push_back(MakeFrame(100.0f, Vec3(0.0f, 2.0f, 0.0f)));
	mesh->blendShapes.push_back(std::move(shape));

	const f32 atFirst[1] = {50.0f};
	const f32 between[1] = {75.0f};
	const f32 atSecond[1] = {100.0f};

	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, atFirst, 0).y, 10.0f);
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, atSecond, 0).y, 2.0f);
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, between, 0).y, 6.0f);

	const f32 quarter[1] = {25.0f};
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, quarter, 0).y, 5.0f);
	Delete(mesh);
}

TEST(BlendShapeKernelTest, NegativeFrameSortsBeforeRestPose) {
	MeshAsset* mesh = MakeTwoVertexMesh();
	MeshBlendShape shape;
	shape.name = "Side";
	shape.frames.push_back(MakeFrame(-50.0f, Vec3(0.0f, -8.0f, 0.0f)));
	shape.frames.push_back(MakeFrame(100.0f, Vec3(0.0f, 4.0f, 0.0f)));
	mesh->blendShapes.push_back(std::move(shape));

	const f32 negative[1] = {-50.0f};
	const f32 rest[1] = {0.0f};
	const f32 halfBack[1] = {-25.0f};

	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, negative, 0).y, -8.0f);
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, rest, 0).y, 0.0f);
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, halfBack, 0).y, -4.0f);
	Delete(mesh);
}

TEST(BlendShapeKernelTest, ShortWeightListRestsRemainder) {
	MeshAsset* mesh = MakeTwoVertexMesh();
	MeshBlendShape first;
	first.name = "A";
	first.frames.push_back(MakeFrame(100.0f, Vec3(0.0f, 4.0f, 0.0f)));
	MeshBlendShape second;
	second.name = "B";
	second.frames.push_back(MakeFrame(100.0f, Vec3(0.0f, 100.0f, 0.0f)));
	mesh->blendShapes.push_back(std::move(first));
	mesh->blendShapes.push_back(std::move(second));

	const f32 onlyFirst[1] = {100.0f};
	EXPECT_FLOAT_EQ(MorphedPosition(*mesh, onlyFirst, 0).y, 4.0f);
	EXPECT_FALSE(GraphicsInternal::HasActiveBlendShapes(*mesh, std::span<const f32>{}));

	const f32 resting[2] = {0.0f, 0.0f};
	EXPECT_FALSE(GraphicsInternal::HasActiveBlendShapes(*mesh, resting));
	Delete(mesh);
}

TEST(BlendShapeKernelTest, AMovedNormalComesBackUnitLength) {
	MeshAsset* mesh = MakeTwoVertexMesh();
	MeshBlendShape shape;
	shape.name = "Tilt";
	shape.frames.push_back(MakeFrame(100.0f, Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 3.0f, 0.0f)));
	mesh->blendShapes.push_back(std::move(shape));

	Vector<MeshVertex> out{GetMemoryResource(MemTag::Asset)};
	out.resize(mesh->vertices.size());
	const f32 full[1] = {100.0f};
	GraphicsInternal::ApplyBlendShapes(*mesh, full, out.data());

	EXPECT_NEAR(out[0].normal.Length(), 1.0f, 0.0001f);
	EXPECT_GT(out[0].normal.y, 0.5f);
	Delete(mesh);
}

TEST(BlendShapeKernelTest, CookedMeshKeepsShapes) {
	MeshAsset* mesh = MakeTwoVertexMesh();
	MeshBlendShape withNormals;
	withNormals.name = "Up";
	withNormals.frames.push_back(MakeFrame(100.0f, Vec3(0.0f, 4.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)));
	MeshBlendShape without;
	without.name = "Wide";
	without.frames.push_back(MakeFrame(60.0f, Vec3(2.0f, 0.0f, 0.0f)));
	without.frames.push_back(MakeFrame(100.0f, Vec3(3.0f, 0.0f, 0.0f)));
	mesh->blendShapes.push_back(std::move(withNormals));
	mesh->blendShapes.push_back(std::move(without));

	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Asset)};
	PakStringPool pool;
	CookWriter descriptor(descriptorBytes, pool);
	CookWriter payload(payloadBytes, pool);
	ASSERT_TRUE(mesh->Cook(descriptor, payload));

	Vector<u8> stringBytes{GetMemoryResource(MemTag::Asset)};
	PakWriteStringBlob(stringBytes, pool.Texts());
	PakStringTable table;
	ASSERT_TRUE(PakReadStringBlob(stringBytes.data(), stringBytes.size(), table));

	MeshAsset* loaded = New<MeshAsset>(MemTag::Asset);
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size(), table);
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size(), table);
	ASSERT_TRUE(loaded->LoadCooked(descriptorReader, payloadReader));

	ASSERT_EQ(loaded->blendShapes.size(), 2u);
	EXPECT_EQ(loaded->blendShapes[0].name, "Up");
	EXPECT_EQ(loaded->blendShapes[1].name, "Wide");
	EXPECT_EQ(loaded->FindBlendShape("Wide"), 1);
	EXPECT_EQ(loaded->FindBlendShape("Absent"), -1);

	ASSERT_EQ(loaded->blendShapes[0].frames.size(), 1u);
	EXPECT_EQ(loaded->blendShapes[0].frames[0].normalDeltas.size(), 1u);
	ASSERT_EQ(loaded->blendShapes[1].frames.size(), 2u);
	EXPECT_TRUE(loaded->blendShapes[1].frames[0].normalDeltas.empty());
	EXPECT_FLOAT_EQ(loaded->blendShapes[1].frames[0].weight, 60.0f);
	ASSERT_EQ(loaded->blendShapes[1].frames[1].positionDeltas.size(), 1u);
	EXPECT_FLOAT_EQ(loaded->blendShapes[1].frames[1].positionDeltas[0].x, 3.0f);

	Delete(mesh);
	Delete(loaded);
}

TEST_F(SkinningFixture, MorphOnlyMeshGetsWeights) {
	ASSERT_TRUE(StageFixture("morphed.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://morphed.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	Handle<Scene> scene = SceneSystem::CreateScene("Morph");
	Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, scene);
	ASSERT_TRUE(root.IsValid());
	SceneSystem::EndFrame();

	Handle<SkinnedMeshRenderer> renderer = root->GetComponentInChildren<SkinnedMeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	EXPECT_FALSE(renderer->GetSkin().IsValid());

	MeshAsset* mesh = renderer->GetMesh().Get();
	ASSERT_NE(mesh, nullptr);
	ASSERT_EQ(mesh->blendShapes.size(), 2u);
	EXPECT_EQ(mesh->blendShapes[0].name, "Up");
	EXPECT_EQ(mesh->blendShapes[1].name, "Wide");
	ASSERT_FALSE(mesh->blendShapes[0].frames.empty());
	ASSERT_FALSE(mesh->blendShapes[1].frames.empty());
	EXPECT_FALSE(mesh->blendShapes[0].frames[0].normalDeltas.empty());
	EXPECT_TRUE(mesh->blendShapes[1].frames[0].normalDeltas.empty());

	EXPECT_EQ(renderer->GetBlendShapeWeightCount(), 2u);
	for (f32 weight : renderer->GetBlendShapeWeights()) {
		EXPECT_FLOAT_EQ(weight, 0.0f);
	}

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(SkinningFixture, SliderMovesMorphPixels) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(StageFixture("morphed.gltf"));
	Asset* model = AssetDatabase::LoadAsset(Path("project://morphed.gltf"));
	ASSERT_NE(model, nullptr);
	const Guid modelGuid = model->assetId;
	AssetDatabase::UnloadAsset(model);

	Handle<Scene> scene = SceneSystem::CreateScene("MorphDraw");
	Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, scene);
	ASSERT_TRUE(root.IsValid());
	Handle<SkinnedMeshRenderer> renderer = root->GetComponentInChildren<SkinnedMeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc = SkinnedView();

	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> rest{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, rest, size));
	u32 restTop = 0;
	u32 restBottom = 0;
	CountHalves(rest, size, restTop, restBottom);
	if (restTop + restBottom == 0) {
		GTEST_SKIP();
	}
	ASSERT_GT(restBottom, 100u);

	ASSERT_EQ(renderer->GetBlendShapeWeightCount(), 2u);
	renderer->SetBlendShapeWeight(static_cast<u32>(0), 100.0f);

	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> raised{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, raised, size));
	u32 raisedTop = 0;
	u32 raisedBottom = 0;
	CountHalves(raised, size, raisedTop, raisedBottom);

	EXPECT_GT(raisedTop, restTop + 100u) << restTop << ", raised " << raisedTop;
	EXPECT_NEAR(static_cast<f32>(raisedBottom), static_cast<f32>(restBottom), 8.0f);
	EXPECT_NE(rest, raised);

	renderer->SetBlendShapeWeight(static_cast<u32>(0), 0.0f);
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> returned{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, returned, size));
	EXPECT_EQ(rest, returned);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}
