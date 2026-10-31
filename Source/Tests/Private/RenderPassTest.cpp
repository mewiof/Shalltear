#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include "PassProbes.h"

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

	Vector<char> _recorded{GetMemoryResource(MemTag::Graphics)};

	class VolumeProbePass : public RenderPass {
	public:
		Handle<ShaderAsset> shader;
		Vector<u8> lattice{GetMemoryResource(MemTag::Graphics)};
		Vec3I size{2, 2, 2};
		u64 revision = 0;
		Vec3 coord{0.25f, 0.25f, 0.25f};
		bool declared = false;
		bool drew = false;

		void DeclarePlan(PassPlan& plan) override {
			declared = plan.DeclareVolume(RHI::Format::RGBA8Unorm, size,
										  std::span<const u8>(lattice.data(), lattice.size()), revision);
		}

		void Record(PassContext& context) override {
			const f32 push[4] = {coord.x, coord.y, coord.z, 0.0f};
			drew = context.DrawProcedural(shader, std::span<const u8>(reinterpret_cast<const u8*>(push), sizeof(push)));
		}
	};

	void SetCell(Vector<u8>& lattice, Vec3I size, i32 x, i32 y, i32 z, u8 r, u8 g, u8 b) {
		const size_t index = ((static_cast<size_t>(z) * size.y + y) * size.x + x) * 4;
		lattice[index] = r;
		lattice[index + 1] = g;
		lattice[index + 2] = b;
		lattice[index + 3] = 255;
	}

	class OrderProbePass : public RenderPass {
	public:
		char label = '?';

		void Record(PassContext& context) override {
			(void)context;
			_recorded.push_back(label);
		}
	};

	class RenderPassFixture : public ::testing::Test {
	protected:
		bool _mounted = false;
		Handle<ShaderAsset> _shader;
		Handle<ShaderAsset> _volumeShader;
		bool _fixturesMounted = false;
		std::filesystem::path _root;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec2I _size{256, 256};

		void SetUp() override {
			_recorded.clear();
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			std::filesystem::path fixtures = FindRepoDir("Source/Tests/Fixtures");
			if (!fixtures.empty()) {
				static std::atomic<u64> counter{0};
				const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
				_root = std::filesystem::temp_directory_path() /
						("shalltear_passrow_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
				std::error_code ec;
				std::filesystem::create_directories(_root / "Graphics", ec);
				std::filesystem::copy_file(fixtures / "Graphics" / "VolumeProbe.hlsl",
										   _root / "Graphics" / "VolumeProbe.hlsl", ec);
				if (!ec) {
					FileSystem::Mount("project", Path(_root.generic_string()));
					_fixturesMounted = true;
				}
			}
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (RenderSystem::IsRenderingAvailable()) {
				_target = RenderSystem::CreateViewTarget(_size);
				if (Asset* asset = AssetDatabase::LoadAsset(Path("engine://Shaders/EditorGrid.hlsl"))) {
					if (asset->GetAssetTypeId() == StringHash("ShaderAsset")) {
						_shader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(asset));
					}
				}
				if (Asset* asset = AssetDatabase::LoadAsset(Path("project://Graphics/VolumeProbe.hlsl"))) {
					if (asset->GetAssetTypeId() == StringHash("ShaderAsset")) {
						_volumeShader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(asset));
					}
				}
			}
		}

		void TearDown() override {
			if (Asset* asset = _shader.Get()) {
				AssetDatabase::UnloadAsset(asset);
			}
			_shader = {};
			if (Asset* asset = _volumeShader.Get()) {
				AssetDatabase::UnloadAsset(asset);
			}
			_volumeShader = {};
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
			if (_fixturesMounted) {
				FileSystem::Unmount("project");
			}
			if (!_root.empty()) {
				std::error_code ec;
				std::filesystem::remove_all(_root, ec);
			}
		}

		bool Ready() const { return _target != INVALID_VIEW_TARGET && _shader.IsValid(); }

		void BuildCamera(Mat4& outView, Mat4& outProjection, Vec3& outPosition) const {
			outPosition = Vec3(0.0f, 6.0f, -6.0f);
			const Quat rotation = Ops::BuildCameraRotation(0.0f, 35.0f);
			outView = Ops::BuildViewMatrix(outPosition, rotation);
			outProjection = Ops::BuildProjection(static_cast<f32>(_size.x) / static_cast<f32>(_size.y));
		}

		void RenderRows(std::span<const Handle<RenderPass>> rows) {
			Mat4 view, projection;
			Vec3 position;
			BuildCamera(view, projection, position);
			ViewDesc desc;
			desc.view = view;
			desc.projection = projection;
			desc.cameraPosition = position;
			desc.clearColor = Color::Black();
			desc.passes = rows;
			RenderSystem::SubmitView(_target, desc);
			RenderSystem::RenderAll();
		}

		str Recorded() const { return str(_recorded.begin(), _recorded.end()); }

		u32 LitBelowHorizon() {
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			EXPECT_TRUE(RenderSystem::ReadbackViewTarget(_target, pixels, size));
			u32 lit = 0;
			for (i32 y = size.y / 2; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
					lit += (static_cast<u32>(pixels[index]) + pixels[index + 1] + pixels[index + 2]) > 12 ? 1u : 0u;
				}
			}
			return lit;
		}

		bool CenterRGB(u32& outR, u32& outG, u32& outB) {
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!RenderSystem::ReadbackViewTarget(_target, pixels, size) || size.x <= 0 || size.y <= 0) {
				return false;
			}
			const size_t index = (static_cast<size_t>(size.y / 2) * size.x + size.x / 2) * 4;
			outR = pixels[index];
			outG = pixels[index + 1];
			outB = pixels[index + 2];
			return true;
		}
	};

} // anonymous namespace

TEST_F(RenderPassFixture, PointOrdersRows) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	OrderProbePass late;
	late.label = 'S';
	late.point = PassPoint::Overlay;
	OrderProbePass middle;
	middle.label = 'O';
	middle.point = PassPoint::AfterOpaque;
	OrderProbePass early;
	early.label = 'P';
	early.point = PassPoint::BeforeOpaque;

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&late), Handle<RenderPass>(&middle), Handle<RenderPass>(&early)};
	RenderRows(rows);
	EXPECT_EQ(Recorded(), "POS");
}

TEST_F(RenderPassFixture, PositionThenDeclarationOrder) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	OrderProbePass a;
	a.label = 'a';
	a.point = PassPoint::AfterOpaque;
	a.position = FrameStagePosition::Late;
	OrderProbePass b;
	b.label = 'b';
	b.point = PassPoint::AfterOpaque;
	b.position = FrameStagePosition::Normal;
	OrderProbePass c;
	c.label = 'c';
	c.point = PassPoint::AfterOpaque;
	c.position = FrameStagePosition::Normal;
	OrderProbePass d;
	d.label = 'd';
	d.point = PassPoint::AfterOpaque;
	d.position = FrameStagePosition::Early;

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&a), Handle<RenderPass>(&b),
									   Handle<RenderPass>(&c), Handle<RenderPass>(&d)};
	RenderRows(rows);
	EXPECT_EQ(Recorded(), "dbca");
}

TEST_F(RenderPassFixture, OpaquePassWipesEarlierRow) {
	if (!Ready()) {
		GTEST_SKIP() << "no device or no shader";
	}
	Mat4 view, projection;
	Vec3 position;
	BuildCamera(view, projection, position);
	const PassProbes::GridPush push = PassProbes::MakeVisiblePush(view, projection, position);

	PassProbes::DrawingProbePass row;
	row.shader = _shader;
	row.bytes = {reinterpret_cast<const u8*>(&push), PassProbes::GRID_PUSH_BYTES};
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};

	row.point = PassPoint::AfterOpaque;
	RenderRows(rows);
	ASSERT_TRUE(row.drew);
	const u32 litAfter = LitBelowHorizon();
	EXPECT_GT(litAfter, 200u);

	row.point = PassPoint::BeforeOpaque;
	RenderRows(rows);
	ASSERT_TRUE(row.drew);
	EXPECT_EQ(LitBelowHorizon(), 0u);
}

TEST_F(RenderPassFixture, FrameLevelPointOnViewRefused) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	OrderProbePass frameLevel;
	frameLevel.label = 'f';
	frameLevel.point = PassPoint::Skinning;
	OrderProbePass ordinary;
	ordinary.label = 'v';
	ordinary.point = PassPoint::AfterOpaque;

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&frameLevel), Handle<RenderPass>(&ordinary)};
	RenderRows(rows);
	EXPECT_EQ(Recorded(), "v");
}

TEST_F(RenderPassFixture, DestroyedRowSkipped) {
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	OrderProbePass survivor;
	survivor.label = 's';
	survivor.point = PassPoint::AfterOpaque;
	auto* doomed = new OrderProbePass();
	doomed->label = 'd';
	doomed->point = PassPoint::AfterOpaque;

	Mat4 view, projection;
	Vec3 position;
	BuildCamera(view, projection, position);
	ViewDesc desc;
	desc.view = view;
	desc.projection = projection;
	desc.cameraPosition = position;
	desc.clearColor = Color::Black();
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(doomed), Handle<RenderPass>(&survivor)};
	desc.passes = rows;
	RenderSystem::SubmitView(_target, desc);

	delete doomed;
	RenderSystem::RenderAll();
	EXPECT_EQ(Recorded(), "s");
}

namespace {

	PassPlan MakePlan() { return PassPlan(Vec2I{256, 256}, Vec3{}); }

} // namespace

TEST(PassPlanTest, ChainKeepsDeclarationOrder) {
	PassPlan plan = MakePlan();
	const u8 half = plan.AddTarget(RHI::Format::RGBA16Float, 2);
	const u8 quarter = plan.AddTarget(RHI::Format::RGBA16Float, 4);
	EXPECT_EQ(half, 0);
	EXPECT_EQ(quarter, 1);
	EXPECT_EQ(plan.GetTargetCount(), 2u);
	EXPECT_EQ(plan.GetTarget(1).divisor, 4);

	EXPECT_TRUE(plan.AddStep(half));
	EXPECT_TRUE(plan.AddStep(quarter, half));
	EXPECT_TRUE(plan.AddStep(PASS_SCENE_TARGET, quarter, half));
	ASSERT_EQ(plan.GetStepCount(), 3u);
	EXPECT_EQ(plan.GetStep(0).target, half);
	EXPECT_EQ(plan.GetStep(0).reads[0], PASS_NO_READ);
	EXPECT_EQ(plan.GetStep(1).reads[0], half);
	EXPECT_EQ(plan.GetStep(2).target, PASS_SCENE_TARGET);
	EXPECT_EQ(plan.GetStep(2).reads[1], half);
}

TEST(PassPlanTest, RowThatDeclaresNothingLeavesEmptyPlan) {
	PassPlan plan = MakePlan();
	EXPECT_EQ(plan.GetTargetCount(), 0u);
	EXPECT_EQ(plan.GetStepCount(), 0u);
}

TEST(PassPlanTest, UndeclaredWriteDropped) {
	PassPlan plan = MakePlan();
	plan.AddTarget(RHI::Format::RGBA16Float, 2);
	EXPECT_FALSE(plan.AddStep(4));
	EXPECT_EQ(plan.GetStepCount(), 0u);
}

TEST(PassPlanTest, UnwrittenReadDropped) {
	PassPlan plan = MakePlan();
	const u8 first = plan.AddTarget(RHI::Format::RGBA16Float, 2);
	const u8 second = plan.AddTarget(RHI::Format::RGBA16Float, 4);
	EXPECT_FALSE(plan.AddStep(first, second));
	EXPECT_EQ(plan.GetStepCount(), 0u);

	EXPECT_TRUE(plan.AddStep(second));
	EXPECT_TRUE(plan.AddStep(first, second));
	EXPECT_EQ(plan.GetStepCount(), 2u);
}

TEST(PassPlanTest, SelfSamplingStepDropped) {
	PassPlan plan = MakePlan();
	const u8 target = plan.AddTarget(RHI::Format::RGBA16Float, 2);
	ASSERT_TRUE(plan.AddStep(target));
	EXPECT_FALSE(plan.AddStep(target, target));
	EXPECT_EQ(plan.GetStepCount(), 1u);
}

TEST(PassPlanTest, ZeroDivisorClampedTo1) {
	PassPlan plan = MakePlan();
	const u8 target = plan.AddTarget(RHI::Format::RGBA16Float, 0);
	ASSERT_NE(target, PASS_NO_READ);
	EXPECT_EQ(plan.GetTarget(target).divisor, 1);
}

TEST(PassPlanTest, ARowThatDeclaresNothingProducesNothing) {
	PassPlan plan = MakePlan();
	EXPECT_EQ(plan.GetProduced(), PassProduces::None);
	EXPECT_EQ(plan.GetProducedTarget(PassProduces::AmbientOcclusion), PASS_NO_READ);
}

TEST(PassPlanTest, ProducedTargetReadByKind) {
	PassPlan plan = MakePlan();
	const u8 first = plan.AddTarget(RHI::Format::R8Unorm, 2);
	const u8 second = plan.AddTarget(RHI::Format::R8Unorm, 2);
	ASSERT_NE(first, PASS_NO_READ);
	ASSERT_NE(second, PASS_NO_READ);
	EXPECT_TRUE(plan.Produce(PassProduces::AmbientOcclusion, second));
	EXPECT_TRUE(HasFlag(plan.GetProduced(), PassProduces::AmbientOcclusion));
	EXPECT_EQ(plan.GetProducedTarget(PassProduces::AmbientOcclusion), second);
}

TEST(PassPlanTest, ProductionMayPrecedeWrite) {
	PassPlan plan = MakePlan();
	const u8 target = plan.AddTarget(RHI::Format::R8Unorm, 2);
	ASSERT_NE(target, PASS_NO_READ);
	EXPECT_TRUE(plan.Produce(PassProduces::AmbientOcclusion, target));
	EXPECT_TRUE(plan.AddStep(target));
	EXPECT_EQ(plan.GetProducedTarget(PassProduces::AmbientOcclusion), target);
}

TEST(PassPlanTest, UndeclaredProductionDropped) {
	PassPlan plan = MakePlan();
	EXPECT_FALSE(plan.Produce(PassProduces::AmbientOcclusion, 0));
	EXPECT_EQ(plan.GetProduced(), PassProduces::None);
}

TEST(PassPlanTest, ProducingTheSceneTargetIsDropped) {
	PassPlan plan = MakePlan();
	const u8 target = plan.AddTarget(RHI::Format::R8Unorm, 1);
	ASSERT_NE(target, PASS_NO_READ);
	EXPECT_FALSE(plan.Produce(PassProduces::AmbientOcclusion, PASS_SCENE_TARGET));
	EXPECT_EQ(plan.GetProduced(), PassProduces::None);
}

TEST(PassPlanTest, ProducingOneKindTwiceKeepsFirstTarget) {
	PassPlan plan = MakePlan();
	const u8 first = plan.AddTarget(RHI::Format::R8Unorm, 2);
	const u8 second = plan.AddTarget(RHI::Format::R8Unorm, 2);
	ASSERT_NE(first, PASS_NO_READ);
	ASSERT_NE(second, PASS_NO_READ);
	EXPECT_TRUE(plan.Produce(PassProduces::AmbientOcclusion, first));
	EXPECT_FALSE(plan.Produce(PassProduces::AmbientOcclusion, second));
	EXPECT_EQ(plan.GetProducedTarget(PassProduces::AmbientOcclusion), first);
}

TEST(PassPlanTest, ProducingNoKindAtAllIsDropped) {
	PassPlan plan = MakePlan();
	const u8 target = plan.AddTarget(RHI::Format::R8Unorm, 2);
	ASSERT_NE(target, PASS_NO_READ);
	EXPECT_FALSE(plan.Produce(PassProduces::None, target));
	EXPECT_EQ(plan.GetProduced(), PassProduces::None);
	EXPECT_EQ(plan.GetProducedTarget(PassProduces::None), PASS_NO_READ);
}

TEST_F(RenderPassFixture, VolumeThirdAxisSelectsCell) {
	if (!Ready() || !_volumeShader.IsValid()) {
		GTEST_SKIP() << "no device or no volume probe shader";
	}
	VolumeProbePass row;
	row.shader = _volumeShader;
	row.size = {2, 2, 2};
	row.lattice.assign(static_cast<size_t>(2 * 2 * 2 * 4), 0);
	SetCell(row.lattice, row.size, 0, 0, 0, 255, 0, 0);
	SetCell(row.lattice, row.size, 0, 0, 1, 0, 0, 255);
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};

	row.coord = {0.25f, 0.25f, 0.25f};
	RenderRows(rows);
	ASSERT_TRUE(row.declared);
	ASSERT_TRUE(row.drew);
	u32 r0 = 0, g0 = 0, b0 = 0;
	ASSERT_TRUE(CenterRGB(r0, g0, b0));
	EXPECT_GT(r0, b0);

	row.coord = {0.25f, 0.25f, 0.75f};
	RenderRows(rows);
	u32 r1 = 0, g1 = 0, b1 = 0;
	ASSERT_TRUE(CenterRGB(r1, g1, b1));
	EXPECT_GT(b1, r1);
}

TEST_F(RenderPassFixture, VolumeUploadsOnlyWhenRevisionMoves) {
	if (!Ready() || !_volumeShader.IsValid()) {
		GTEST_SKIP() << "no device or no volume probe shader";
	}
	VolumeProbePass row;
	row.shader = _volumeShader;
	row.size = {2, 2, 2};
	row.lattice.assign(static_cast<size_t>(2 * 2 * 2 * 4), 0);
	SetCell(row.lattice, row.size, 0, 0, 0, 255, 0, 0);
	row.coord = {0.25f, 0.25f, 0.25f};
	row.revision = 1;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};

	RenderRows(rows);
	u32 r0 = 0, g0 = 0, b0 = 0;
	ASSERT_TRUE(CenterRGB(r0, g0, b0));
	EXPECT_GT(r0, b0);

	SetCell(row.lattice, row.size, 0, 0, 0, 0, 0, 255);
	RenderRows(rows);
	u32 r1 = 0, g1 = 0, b1 = 0;
	ASSERT_TRUE(CenterRGB(r1, g1, b1));
	EXPECT_GT(r1, b1);

	row.revision = 2;
	RenderRows(rows);
	u32 r2 = 0, g2 = 0, b2 = 0;
	ASSERT_TRUE(CenterRGB(r2, g2, b2));
	EXPECT_GT(b2, r2);
}

TEST_F(RenderPassFixture, VolumeFreedWithRow) {
	if (!Ready() || !_volumeShader.IsValid()) {
		GTEST_SKIP() << "no device or no volume probe shader";
	}
	RHI::Device* device = RenderSystem::GetDevice();
	ASSERT_NE(device, nullptr);

	RenderRows(std::span<const Handle<RenderPass>>{});
	const u32 before = device->GetLiveTextureCount();
	{
		VolumeProbePass row;
		row.shader = _volumeShader;
		row.size = {2, 2, 2};
		row.lattice.assign(static_cast<size_t>(2 * 2 * 2 * 4), 255);
		const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
		RenderRows(rows);
		ASSERT_TRUE(row.declared);
		EXPECT_EQ(device->GetLiveTextureCount(), before + 1);
	}
	RenderRows(std::span<const Handle<RenderPass>>{});
	EXPECT_EQ(device->GetLiveTextureCount(), before);
}
