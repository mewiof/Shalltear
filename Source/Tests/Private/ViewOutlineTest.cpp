#include "ShadingHarness.h"

#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Editor/EditorViewportOps.h"

using namespace ShadingTest;

namespace {

	constexpr const char* DISPLACED_SHADER = "#define SHALLTEAR_VERTEX_FUNCTION\n"
											 "#pragma shalltear_template lit\n"
											 "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 offset; };\n"
											 "void ShalltearVertexFunction(ShalltearVertexInput input, inout ShalltearVertexOutput vertex) {\n"
											 "\tvertex.positionOS = input.positionOS + offset.xyz;\n"
											 "}\n"
											 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {}\n";

	constexpr const char* PLAIN_SHADER = "#pragma shalltear_template lit\n"
										 "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 offset; };\n"
										 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {}\n";

	constexpr Color32 OUTLINE{0, 255, 0, 255};

	bool IsOutline(const Vector<u8>& pixels, size_t texel) {
		const size_t at = texel * 4;
		return pixels[at + 1] > 200 && pixels[at] < 60 && pixels[at + 2] < 60;
	}

	u32 CountOutline(const Vector<u8>& pixels) {
		u32 count = 0;
		for (size_t texel = 0; texel * 4 < pixels.size(); ++texel) {
			count += IsOutline(pixels, texel) ? 1u : 0u;
		}
		return count;
	}

	u32 DifferingTexels(const Vector<u8>& a, const Vector<u8>& b) {
		u32 differing = 0;
		for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
			differing += (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ? 1u : 0u;
		}
		return differing;
	}

	class ViewOutlineFixture : public MaterialReadbackFixture {
	protected:
		struct Placed {
			MaterialAsset* material = nullptr;
			Vec3 position{};
			bool outlined = false;
		};

		bool Render(std::span<const Placed> cubes, f32 hiddenOpacity, Vector<u8>& pixels, Vec2I& size) {
			Asset* cubeAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
			if (!cubeAsset) {
				return false;
			}
			Handle<MeshAsset> cube(static_cast<MeshAsset*>(cubeAsset));
			Handle<Scene> scene = SceneSystem::CreateScene("Outlined");
			Vector<ViewOutline> outlines{GetMemoryResource(MemTag::Graphics)};
			for (const Placed& placed : cubes) {
				Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
				object->SetLocalPosition(placed.position);
				Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
				renderer->SetMesh(cube);
				renderer->SetMaterial(0, Handle<MaterialAsset>(placed.material));
				if (placed.outlined) {
					outlines.push_back({object, OUTLINE});
				}
			}
			Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
			sun->SetLocalRotation(Quat::FromEuler(Vec3(52.0f, 37.0f, 11.0f)));
			sun->AddComponent<Light>()->SetIntensity(2.0f);
			SceneSystem::EndFrame();

			ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
			if (target == INVALID_VIEW_TARGET) {
				return false;
			}
			ViewDesc desc;
			desc.view = Mat4::LookAt(Vec3(0.0f, 2.0f, -9.0f), Vec3(1.0f, 0.5f, 0.0f));
			desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
			desc.cameraPosition = Vec3(0.0f, 2.0f, -9.0f);
			desc.clearColor = Color::Black();
			desc.tonemap = false;
			desc.outlines = {outlines.data(), outlines.size()};
			desc.outlineWidth = 3.0f;
			desc.outlineHiddenOpacity = hiddenOpacity;
			RenderSystem::SubmitView(target, desc);
			RenderSystem::RenderAll();
			const bool read = RenderSystem::ReadbackViewTarget(target, pixels, size);
			RenderSystem::DestroyViewTarget(target);
			SceneSystem::UnloadScene(scene);
			SceneSystem::EndFrame();
			return read;
		}

		void RequireDeviceAndCompiler() {
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			if (!CompilerAvailable()) {
				GTEST_SKIP() << "dxcompiler.dll unavailable";
			}
		}
	};

} // namespace

TEST_F(ViewOutlineFixture, OutlineFollowsMovedSurface) {
	RequireDeviceAndCompiler();
	if (IsSkipped()) {
		return;
	}
	WriteProjectFile("Displaced.hlsl", DISPLACED_SHADER);
	WriteProjectFile("Plain.hlsl", PLAIN_SHADER);
	Asset* displacedShader = AssetDatabase::LoadAsset(Path("project://Displaced.hlsl"));
	Asset* plainShader = AssetDatabase::LoadAsset(Path("project://Plain.hlsl"));
	ASSERT_NE(displacedShader, nullptr);
	ASSERT_NE(plainShader, nullptr);
	MaterialAsset* displaced = LoadMaterial("Displaced.mat", displacedShader->assetId, "\t\"colors\": { \"offset\": [2, 0, 0, 0] }");
	MaterialAsset* resting = LoadMaterial("Resting.mat", displacedShader->assetId, "\t\"colors\": { \"offset\": [0, 0, 0, 0] }");
	MaterialAsset* plain = LoadMaterial("Plain.mat", plainShader->assetId, "");
	ASSERT_NE(displaced, nullptr);
	ASSERT_NE(resting, nullptr);
	ASSERT_NE(plain, nullptr);

	const Vec3 rest(0.0f, 0.5f, 0.0f);
	const Vec3 moved(2.0f, 0.5f, 0.0f);
	Vector<u8> byFunction{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> byTransform{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> atRest{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> unoutlined{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	const Placed displacedCube[] = {{displaced, rest, true}};
	const Placed movedCube[] = {{plain, moved, true}};
	const Placed restingCube[] = {{resting, rest, true}};
	const Placed bareCube[] = {{plain, moved, false}};
	ASSERT_TRUE(Render(displacedCube, 1.0f, byFunction, size));
	ASSERT_TRUE(Render(movedCube, 1.0f, byTransform, size));
	ASSERT_TRUE(Render(restingCube, 1.0f, atRest, size));
	ASSERT_TRUE(Render(bareCube, 1.0f, unoutlined, size));

	const u32 ring = CountOutline(byTransform);
	EXPECT_GT(ring, 60u);
	EXPECT_EQ(CountOutline(unoutlined), 0u);
	EXPECT_EQ(DifferingTexels(byFunction, byTransform), 0u);
	EXPECT_GT(DifferingTexels(byFunction, atRest), 60u);
	u32 inside = 0;
	for (size_t texel = 0; texel * 4 < byTransform.size(); ++texel) {
		const size_t at = texel * 4;
		const bool covered = unoutlined[at] != 0 || unoutlined[at + 1] != 0 || unoutlined[at + 2] != 0;
		inside += IsOutline(byTransform, texel) && covered ? 1u : 0u;
	}
	EXPECT_EQ(inside, 0u);
}

TEST_F(ViewOutlineFixture, OutlineShowsHiddenObject) {
	RequireDeviceAndCompiler();
	if (IsSkipped()) {
		return;
	}
	WriteProjectFile("Plain.hlsl", PLAIN_SHADER);
	Asset* plainShader = AssetDatabase::LoadAsset(Path("project://Plain.hlsl"));
	ASSERT_NE(plainShader, nullptr);
	MaterialAsset* plain = LoadMaterial("Plain.mat", plainShader->assetId, "");
	ASSERT_NE(plain, nullptr);

	const Placed scene[] = {{plain, Vec3(1.0f, 0.5f, 2.0f), true}, {plain, Vec3(0.0f, 0.5f, -2.0f), false}};
	Vector<u8> shown{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> hiddenOff{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(scene, 1.0f, shown, size));
	ASSERT_TRUE(Render(scene, 0.0f, hiddenOff, size));

	const u32 everywhere = CountOutline(shown);
	const u32 visibleOnly = CountOutline(hiddenOff);
	EXPECT_GT(visibleOnly, 20u);
	EXPECT_GT(everywhere, visibleOnly + 20u);
}

namespace {

	class SelectionOutlineFixture : public ::testing::Test {
	protected:
		void SetUp() override { SceneSystem::Initialize(); }
		void TearDown() override {
			SceneSystem::UnloadAllScenes();
			SceneSystem::EndFrame();
			SceneSystem::Terminate();
		}
	};

} // namespace

TEST_F(SelectionOutlineFixture, OutlinesCoverSubtrees) {
	Handle<Scene> scene = SceneSystem::CreateScene("Selection");
	Handle<SceneObject> root = SceneSystem::CreateObject("Root", scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("Child", scene);
	Handle<SceneObject> grandchild = SceneSystem::CreateObject("Grandchild", scene);
	Handle<SceneObject> other = SceneSystem::CreateObject("Other", scene);
	child->SetParent(root);
	grandchild->SetParent(child);
	Handle<MeshRenderer> otherRenderer = other->AddComponent<MeshRenderer>();
	SceneSystem::EndFrame();

	constexpr Color32 SELECTED{255, 102, 0, 255};
	constexpr Color32 CHILDREN{94, 119, 155, 255};
	const Handle<Object> selection[] = {Handle<Object>(root.Get()), Handle<Object>(otherRenderer.Get()), Handle<Object>(grandchild.Get())};
	Vector<ViewOutline> outlines{GetMemoryResource(MemTag::Editor)};
	EditorViewportOps::BuildSelectionOutlines(selection, SELECTED, CHILDREN, outlines);

	ASSERT_EQ(outlines.size(), 5u);
	EXPECT_EQ(outlines[0].object, root);
	EXPECT_EQ(outlines[1].object, other);
	EXPECT_EQ(outlines[2].object, grandchild);
	for (u32 index = 0; index < 3; ++index) {
		EXPECT_EQ(outlines[index].color, SELECTED) << "selected entry " << index;
	}
	EXPECT_EQ(outlines[3].object, child);
	EXPECT_EQ(outlines[4].object, grandchild);
	EXPECT_EQ(outlines[3].color, CHILDREN);
	EXPECT_EQ(outlines[4].color, CHILDREN);

	outlines.push_back({});
	EditorViewportOps::BuildSelectionOutlines({}, SELECTED, CHILDREN, outlines);
	EXPECT_TRUE(outlines.empty());
}
