#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/RHITypes.h"

using namespace ShadingTest;

namespace {

	constexpr const char* DISPLACED_SHADER = "#define SHALLTEAR_VERTEX_FUNCTION\n"
											 "#pragma shalltear_template lit\n"
											 "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 offset; };\n"
											 "void ShalltearVertexFunction(ShalltearVertexInput input, inout ShalltearVertexOutput vertex) {\n"
											 "\tvertex.positionOS = input.positionOS + offset.xyz;\n"
											 "}\n"
											 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {}\n";

	constexpr Vec2I TARGET_SIZE{128, 128};
	const Vec3 EYE(0.0f, 2.0f, -9.0f);

	class ViewPickFixture : public MaterialReadbackFixture {
	protected:
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Handle<Scene> _scene;
		ViewDesc _desc;

		void TearDown() override {
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			MaterialReadbackFixture::TearDown();
		}

		Handle<SceneObject> Stage(MaterialAsset* material, Vec3 position) {
			Asset* cubeAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
			if (!cubeAsset) {
				return {};
			}
			_scene = SceneSystem::CreateScene("Picked");
			Handle<SceneObject> cube = SceneSystem::CreateObject("Cube", _scene);
			cube->SetLocalPosition(position);
			Handle<MeshRenderer> renderer = cube->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(cubeAsset)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();

			_target = RenderSystem::CreateViewTarget(TARGET_SIZE);
			_desc.view = Mat4::LookAt(EYE, Vec3(1.0f, 0.5f, 0.0f));
			_desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
			_desc.cameraPosition = EYE;
			return cube;
		}

		Vec2I PixelOf(Vec3 world) const {
			const Vec4 clip = (_desc.projection * _desc.view) * Vec4(world, 1.0f);
			const f32 x = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<f32>(TARGET_SIZE.x);
			const f32 y = (0.5f - clip.y / clip.w * 0.5f) * static_cast<f32>(TARGET_SIZE.y);
			return Vec2I(static_cast<i32>(x), static_cast<i32>(y));
		}

		void Frame(const ViewPick* pick = nullptr) {
			ViewDesc desc = _desc;
			desc.pick = pick;
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
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

TEST_F(ViewPickFixture, PickAnswersAfterFrame) {
	RequireDeviceAndCompiler();
	if (IsSkipped()) {
		return;
	}
	WriteProjectFile("Displaced.hlsl", DISPLACED_SHADER);
	Asset* shader = AssetDatabase::LoadAsset(Path("project://Displaced.hlsl"));
	ASSERT_NE(shader, nullptr);
	MaterialAsset* displaced = LoadMaterial("Displaced.mat", shader->assetId, "\t\"colors\": { \"offset\": [2, 0, 0, 0] }");
	ASSERT_NE(displaced, nullptr);

	const Vec3 rest(0.0f, 0.5f, 0.0f);
	Handle<SceneObject> cube = Stage(displaced, rest);
	ASSERT_TRUE(cube.IsValid());
	ASSERT_NE(_target, INVALID_VIEW_TARGET);

	const ViewPick drawn{PixelOf(rest + Vec3(2.0f, 0.0f, 0.0f)), 11};
	const ViewPick resting{PixelOf(rest), 22};
	Frame(&drawn);
	Frame(&resting);
	PickAnswer answer;
	u32 frames = 0;
	while (!RenderSystem::TakePickAnswer(_target, answer) && frames < RHI::FRAMES_IN_FLIGHT) {
		Frame();
		++frames;
	}
	EXPECT_GT(frames, 0u);
	EXPECT_EQ(answer.tag, 11u);
	EXPECT_EQ(answer.object, cube);

	while (!RenderSystem::TakePickAnswer(_target, answer) && frames < 2 * RHI::FRAMES_IN_FLIGHT) {
		Frame();
		++frames;
	}
	EXPECT_EQ(answer.tag, 22u);
	EXPECT_FALSE(answer.object.IsValid());
	EXPECT_FALSE(RenderSystem::TakePickAnswer(_target, answer));
}
