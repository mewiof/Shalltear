#include "TestsPCH.h"

#include "Graphics/GraphicsSettings.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include "PassProbes.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace Ops = Shalltear::EditorViewportOps;

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
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

	using PassProbes::DrawingProbePass;
	using PassProbes::GridPush;

	class EditorGridFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		bool _projectMounted = false;
		std::filesystem::path _root;
		Handle<ShaderAsset> _shader;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec2I _size{256, 256};

		virtual i32 SampleCount() const { return 1; }

		void SetUp() override {
			const i32 samples = SampleCount();
			if (samples > 1) {
				static std::atomic<u64> counter{0};
				const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
				_root = std::filesystem::temp_directory_path() /
						("shalltear_grid_msaa_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
				std::filesystem::create_directories(_root / "Assets");
				SettingsPaths paths;
				paths.project = Path((_root / "ProjectSettings.ini").generic_string());
				Settings::Initialize(paths);
			}

			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			if (samples > 1) {
				FileSystem::Mount("project", Path(_root.generic_string()));
				_projectMounted = true;
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (RenderSystem::IsRenderingAvailable()) {
				if (samples > 1) {
					const Guid profileId =
						AssetDatabase::CreateAsset(Path("project://Assets/Msaa.slprofile"), StringHash("GraphicsProfile"));
					ASSERT_TRUE(profileId.IsValid());
					auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(profileId));
					ASSERT_NE(profile, nullptr);
					profile->msaa = samples;
					GraphicsSettings* settings =
						Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
					ASSERT_NE(settings, nullptr);
					settings->qualityProfiles = profileId.ToString();
					settings->qualityLevel = 0;
					Settings::NotifyChanged("Graphics", "qualityLevel");
					RenderSystem::RenderAll();
				}
				_target = RenderSystem::CreateViewTarget(_size);
				if (Asset* asset = AssetDatabase::LoadAsset(Path("engine://Shaders/EditorGrid.hlsl"))) {
					if (asset->GetAssetTypeId() == StringHash("ShaderAsset")) {
						_shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(asset));
					}
				}
			}
		}

		void TearDown() override {
			if (Asset* asset = _shader.Get()) {
				AssetDatabase::UnloadAsset(asset);
			}
			_shader = {};
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			if (_projectMounted) {
				FileSystem::Unmount("project");
				Settings::Terminate();
				std::error_code ec;
				std::filesystem::remove_all(_root, ec);
			}
		}

		bool Ready() const { return _target != INVALID_VIEW_TARGET && _shader.IsValid(); }

		void BuildCamera(f32 height, f32 pitchDeg, Mat4& outView, Mat4& outProjection, Vec3& outPosition) const {
			outPosition = Vec3(0.0f, height, -height);
			const Quat rotation = Ops::BuildCameraRotation(0.0f, pitchDeg);
			outView = Ops::BuildViewMatrix(outPosition, rotation);
			outProjection = Ops::BuildProjection(static_cast<f32>(_size.x) / static_cast<f32>(_size.y));
		}

		void BuildOrthographicCamera(const Vec3& axis, f32 back, f32 size, Mat4& outView, Mat4& outProjection,
									 Vec3& outPosition, f32 standingYawDeg = 45.0f) const {
			f32 yawDeg = 0.0f;
			f32 pitchDeg = 0.0f;
			Ops::ViewAnglesFromAxis(axis, standingYawDeg, yawDeg, pitchDeg);
			const Quat rotation = Ops::BuildCameraRotation(yawDeg, pitchDeg);
			outPosition = axis.Normalized() * back;

			Ops::Viewpoint viewpoint;
			viewpoint.position = outPosition;
			viewpoint.rotation = rotation;
			viewpoint.aspect = static_cast<f32>(_size.x) / static_cast<f32>(_size.y);
			viewpoint.orthographic = true;
			viewpoint.orthographicSize = size;
			outView = Ops::BuildViewMatrix(viewpoint);
			outProjection = Ops::BuildProjection(viewpoint);
		}

		Vector<u8> RenderGrid(const Mat4& view, const Mat4& projection, const Vec3& position, bool withGrid) {
			return RenderPush(PassProbes::MakeVisiblePush(view, projection, position), view, projection, position,
							  withGrid);
		}

		Vector<u8> RenderGridOnPlane(const Mat4& view, const Mat4& projection, const Vec3& position,
									 Ops::GridPlane plane) {
			const Ops::GridBasis basis = Ops::GridBasisFor(plane);
			GridPush push = PassProbes::MakeVisiblePush(view, projection, position);
			push.invGridProjection = basis.gridFromWorld * (projection * view).Inverse();
			push.gridCameraPosition = basis.gridFromWorld.TransformPoint(position);
			return RenderPush(push, view, projection, position, true);
		}

		Vector<u8> RenderPush(const GridPush& push, const Mat4& view, const Mat4& projection, const Vec3& position,
							  bool withGrid) {
			DrawingProbePass pass;
			pass.shader = _shader;
			pass.bytes = {reinterpret_cast<const u8*>(&push), PassProbes::GRID_PUSH_BYTES};
			const Handle<RenderPass> row(&pass);

			ViewDesc desc;
			desc.view = view;
			desc.projection = projection;
			desc.cameraPosition = position;
			desc.clearColor = Color::Black();
			if (withGrid) {
				desc.passes = {&row, 1};
			}
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();

			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			EXPECT_TRUE(RenderSystem::ReadbackViewTarget(_target, pixels, size));
			EXPECT_EQ(size.x, _size.x);
			return pixels;
		}

		static u32 Luminance(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
			return static_cast<u32>(pixels[index]) + pixels[index + 1] + pixels[index + 2];
		}

		void BuildTopDownCamera(f32 height, Mat4& outView, Mat4& outProjection, Vec3& outPosition) const {
			outPosition = Vec3(31234.5f, height, 47654.5f);
			const Quat rotation = Ops::BuildCameraRotation(45.0f, 90.0f);
			outView = Ops::BuildViewMatrix(outPosition, rotation);
			outProjection = Ops::BuildProjection(static_cast<f32>(_size.x) / static_cast<f32>(_size.y));
		}

		static u32 WidestFullStrengthBand(const Vector<u8>& pixels, Vec2I size) {
			u32 peak = 0;
			for (i32 y = 0; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					peak = Math::Max(peak, Luminance(pixels, size, x, y));
				}
			}
			const u32 threshold = Math::Max(peak / 2, 12u);

			u32 widest = 0;
			for (i32 y = 0; y < size.y; ++y) {
				u32 run = 0;
				for (i32 x = 0; x < size.x; ++x) {
					run = Luminance(pixels, size, x, y) > threshold ? run + 1 : 0;
					widest = Math::Max(widest, run);
				}
			}
			for (i32 x = 0; x < size.x; ++x) {
				u32 run = 0;
				for (i32 y = 0; y < size.y; ++y) {
					run = Luminance(pixels, size, x, y) > threshold ? run + 1 : 0;
					widest = Math::Max(widest, run);
				}
			}
			return widest;
		}

		static u32 LitPixelsBelowHorizon(const Vector<u8>& pixels, Vec2I size) {
			u32 lit = 0;
			for (i32 y = size.y / 2; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					lit += Luminance(pixels, size, x, y) > 12 ? 1u : 0u;
				}
			}
			return lit;
		}
	};

	class EditorGridSamplesFixture : public EditorGridFixture, public ::testing::WithParamInterface<i32> {
	protected:
		i32 SampleCount() const override { return GetParam(); }
	};

} // anonymous namespace

TEST_F(EditorGridFixture, PassRowPaintsOmittedPaintsNothing) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	Mat4 view, projection;
	Vec3 position;
	BuildCamera(6.0f, 35.0f, view, projection, position);

	const Vector<u8> withGrid = RenderGrid(view, projection, position, true);
	const u32 lit = LitPixelsBelowHorizon(withGrid, _size);
	EXPECT_GT(lit, 200u);

	const Vector<u8> withoutGrid = RenderGrid(view, projection, position, false);
	EXPECT_EQ(LitPixelsBelowHorizon(withoutGrid, _size), 0u);
}

TEST_F(EditorGridFixture, OversizedPushRejected) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	Mat4 view, projection;
	Vec3 position;
	BuildCamera(6.0f, 35.0f, view, projection, position);

	u8 tooMany[192] = {};
	DrawingProbePass pass;
	pass.shader = _shader;
	pass.bytes = {tooMany, sizeof(tooMany)};
	const Handle<RenderPass> row(&pass);

	ViewDesc desc;
	desc.view = view;
	desc.projection = projection;
	desc.cameraPosition = position;
	desc.clearColor = Color::Black();
	desc.passes = {&row, 1};
	RenderSystem::SubmitView(_target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(_target, pixels, size));
	EXPECT_EQ(LitPixelsBelowHorizon(pixels, size), 0u);
	EXPECT_FALSE(pass.drew);
}

TEST_F(EditorGridFixture, LinesAreBrighterThanCellsBetweenThem) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	Mat4 view, projection;
	Vec3 position;
	BuildCamera(6.0f, 35.0f, view, projection, position);
	const Vector<u8> pixels = RenderGrid(view, projection, position, true);

	u32 lit = 0;
	u32 dark = 0;
	for (i32 y = _size.y / 2; y < _size.y; ++y) {
		for (i32 x = 0; x < _size.x; ++x) {
			(Luminance(pixels, _size, x, y) > 12 ? lit : dark) += 1;
		}
	}
	EXPECT_GT(lit, 200u);
	EXPECT_GT(dark, 200u);
}

TEST_F(EditorGridFixture, GridKeepsWorkingAsCameraClimbs) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	for (f32 height : {0.5f, 20.0f, 500.0f, 5000.0f}) {
		Mat4 view, projection;
		Vec3 position;
		BuildCamera(height, 35.0f, view, projection, position);
		const Vector<u8> pixels = RenderGrid(view, projection, position, true);

		u32 lit = 0;
		u32 dark = 0;
		for (i32 y = _size.y / 2; y < _size.y; ++y) {
			for (i32 x = 0; x < _size.x; ++x) {
				(Luminance(pixels, _size, x, y) > 12 ? lit : dark) += 1;
			}
		}
		EXPECT_GT(lit, 100u) << height;
		EXPECT_GT(dark, 100u) << height;
	}
}

TEST_F(EditorGridFixture, DecadesDrawSameLineWidth) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	for (f32 height : {2.0f, 7.0f, 30.0f, 90.0f, 400.0f, 1500.0f}) {
		Mat4 view, projection;
		Vec3 position;
		BuildTopDownCamera(height, view, projection, position);
		const Vector<u8> pixels = RenderGrid(view, projection, position, true);

		u32 lit = 0;
		for (i32 y = 0; y < _size.y; ++y) {
			for (i32 x = 0; x < _size.x; ++x) {
				lit += Luminance(pixels, _size, x, y) > 12 ? 1u : 0u;
			}
		}
		ASSERT_GT(lit, 200u) << height;
		EXPECT_LE(WidestFullStrengthBand(pixels, _size), 8u) << "at height " << height;
	}
}

TEST_F(EditorGridFixture, TheLatticeSurvivesAnOrthographicProjection) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	Mat4 view, projection;
	Vec3 position;
	BuildOrthographicCamera(Vec3(0.0f, 1.0f, 0.0f), 60.0f, 14.0f, view, projection, position);
	const Vector<u8> pixels = RenderGrid(view, projection, position, true);

	u32 lit = 0;
	u32 dark = 0;
	for (i32 y = 0; y < _size.y; ++y) {
		for (i32 x = 0; x < _size.x; ++x) {
			(Luminance(pixels, _size, x, y) > 12 ? lit : dark) += 1;
		}
	}
	EXPECT_GT(lit, 200u);
	EXPECT_GT(dark, 200u);
	EXPECT_LE(WidestFullStrengthBand(pixels, _size), 8u);

	EXPECT_EQ(LitPixelsBelowHorizon(RenderGrid(view, projection, position, false), _size), 0u);
}

TEST_F(EditorGridFixture, ViewDownZDrawsXYPlane) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	Ops::Viewpoint viewpoint;
	viewpoint.position = Vec3(0.0f, 25.0f, -60.0f);
	viewpoint.rotation = Ops::BuildCameraRotation(0.0f, 0.0f);
	viewpoint.aspect = static_cast<f32>(_size.x) / static_cast<f32>(_size.y);
	viewpoint.orthographic = true;
	viewpoint.orthographicSize = 14.0f;
	const Mat4 view = Ops::BuildViewMatrix(viewpoint);
	const Mat4 projection = Ops::BuildProjection(viewpoint);
	ASSERT_EQ(Ops::GridPlaneFor(view, projection), Ops::GridPlane::XY);

	const Vector<u8> facing = RenderGrid(view, projection, viewpoint.position, true);
	u32 lit = 0;
	u32 dark = 0;
	for (i32 y = 0; y < _size.y; ++y) {
		for (i32 x = 0; x < _size.x; ++x) {
			(Luminance(facing, _size, x, y) > 12 ? lit : dark) += 1;
		}
	}
	EXPECT_GT(lit, 200u);
	EXPECT_GT(dark, 200u);

	const Vector<u8> ground = RenderGridOnPlane(view, projection, viewpoint.position, Ops::GridPlane::XZ);
	u32 groundLit = 0;
	for (i32 y = 0; y < _size.y; ++y) {
		for (i32 x = 0; x < _size.x; ++x) {
			groundLit += Luminance(ground, _size, x, y) > 12 ? 1u : 0u;
		}
	}
	EXPECT_EQ(groundLit, 0u);
}

TEST_P(EditorGridSamplesFixture, FloorOnPlaneDoesNotZFight) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	if (RenderSystem::GetSampleCount() != static_cast<u32>(GetParam())) {
		GTEST_SKIP() << "adapter doesn't offer " << GetParam() << "x on the scene chain";
	}
	Mat4 view, projection;
	Vec3 position;
	BuildCamera(6.0f, 35.0f, view, projection, position);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	Asset* materialAsset = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
	if (!meshAsset || !materialAsset) {
		GTEST_SKIP();
	}
	MeshAsset* cube = static_cast<MeshAsset*>(meshAsset);
	ASSERT_NE(cube->vertexBufferAddress, 0u);

	Handle<Scene> scene = SceneSystem::CreateScene("GridCoplanar");
	Handle<SceneObject> floor = SceneSystem::CreateObject("Floor", scene);
	Handle<MeshRenderer> renderer = floor->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(cube));
	renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(materialAsset)));
	floor->SetLocalPosition(Vec3(0.0f, -1.0f, 8.0f));
	floor->SetLocalScale(Vec3(120.0f, 2.0f, 120.0f));
	SceneSystem::EndFrame();

	const Vector<u8> floorOnly = RenderGrid(view, projection, position, false);
	ASSERT_GT(RenderSystem::GetStats().triangles, 0u);
	const Vector<u8> floorAndGrid = RenderGrid(view, projection, position, true);

	u32 floorPixels = 0;
	u32 changed = 0;
	for (i32 y = 0; y < _size.y; ++y) {
		for (i32 x = 0; x < _size.x; ++x) {
			const u32 solid = Luminance(floorOnly, _size, x, y);
			if (solid <= 12) {
				continue;
			}
			++floorPixels;
			const u32 both = Luminance(floorAndGrid, _size, x, y);
			changed += (solid > both ? solid - both : both - solid) > 12 ? 1u : 0u;
		}
	}

	ASSERT_GT(floorPixels, 5000u);
	EXPECT_LT(changed, floorPixels / 200);

	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(meshAsset);
}

INSTANTIATE_TEST_SUITE_P(SampleCounts, EditorGridSamplesFixture, ::testing::Values(1, 2, 4, 8),
						 [](const ::testing::TestParamInfo<i32>& info) { return std::to_string(info.param) + "x"; });

TEST_F(EditorGridFixture, GeometryOnPlaneOccludesGridBehindIt) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	Mat4 view, projection;
	Vec3 position;
	BuildCamera(6.0f, 35.0f, view, projection, position);

	Asset* meshAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	Asset* materialAsset = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
	if (!meshAsset || !materialAsset) {
		GTEST_SKIP();
	}
	MeshAsset* cube = static_cast<MeshAsset*>(meshAsset);
	ASSERT_NE(cube->vertexBufferAddress, 0u);

	Handle<Scene> scene = SceneSystem::CreateScene("GridOcclusion");
	Handle<SceneObject> wall = SceneSystem::CreateObject("Wall", scene);
	Handle<MeshRenderer> renderer = wall->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(cube));
	renderer->SetMaterial(0, Handle<MaterialAsset>(static_cast<MaterialAsset*>(materialAsset)));
	wall->SetLocalPosition(Vec3(0.0f, 1.5f, 2.5f));
	wall->SetLocalScale(Vec3(30.0f, 3.0f, 0.5f));
	SceneSystem::EndFrame();

	const Vector<u8> wallOnly = RenderGrid(view, projection, position, false);
	ASSERT_GT(RenderSystem::GetStats().triangles, 0u);
	const Vector<u8> wallAndGrid = RenderGrid(view, projection, position, true);

	u32 wallPixels = 0;
	u32 wallPixelsTheGridPaintedOver = 0;
	u32 openPixelsTheGridPainted = 0;
	for (i32 y = 0; y < _size.y; ++y) {
		for (i32 x = 0; x < _size.x; ++x) {
			const u32 solid = Luminance(wallOnly, _size, x, y);
			const u32 both = Luminance(wallAndGrid, _size, x, y);
			if (solid > 12) {
				++wallPixels;
				wallPixelsTheGridPaintedOver += (solid > both ? solid - both : both - solid) > 12 ? 1u : 0u;
			} else if (both > 12) {
				++openPixelsTheGridPainted;
			}
		}
	}

	ASSERT_GT(wallPixels, 500u);
	ASSERT_GT(openPixelsTheGridPainted, 500u);
	EXPECT_LT(wallPixelsTheGridPaintedOver, wallPixels / 50);

	AssetDatabase::UnloadAsset(materialAsset);
	AssetDatabase::UnloadAsset(meshAsset);
}
