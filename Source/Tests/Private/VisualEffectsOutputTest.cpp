#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "EffectKernelHarness.h"
#include "ShadingHarness.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Module/ModuleAPI.h"

#include "ParticleSimulation.h"

#include <array>
#include <filesystem>

using namespace Shalltear;

SLT_DECLARE_LINKED_PACKAGE(VisualEffects)

namespace {

	using namespace ShadingTest;
	using VisualEffectsImpl::ParticleSystem;
	using VisualEffectsImpl::ParticleSystemDesc;

	class ParticleProbeRenderer : public ProceduralRenderer {
	public:
		SLT_REFLECT_COMPONENT(ParticleProbeRenderer, ProceduralRenderer)
		SLT_END_REFLECT()

		ParticleSystem system;
		AABB localBounds = AABB::FromCenterExtents(Vec3{}, Vec3(2.0f, 2.0f, 2.0f));
		u32 outputs = 1;
		u32 recorded = 0;
		bool drew = false;

		void Declare(ProceduralPlan& plan) override {
			const SceneObject* owner = GetOwner().Get();
			const AABB bounds = localBounds.Transformed(owner ? owner->GetWorldMatrix() : Mat4::Identity());
			plan.SetBounds(bounds);
			for (u32 output = 0; output < outputs; ++output) {
				system.DeclareOutput(plan, bounds.Center(), output);
			}
		}

		void Record(ProceduralContext& context) override {
			++recorded;
			drew = system.RecordOutput(context, context.GetItem());
		}
	};

	SLT_REGISTER_TYPE(ParticleProbeRenderer)

} // anonymous namespace

class ParticleOutputFixture : public MaterialReadbackFixture {
protected:
	char _token = 0;
	ShaderAsset _initialize;
	ShaderAsset _update;
	ShaderAsset _quad;
	ShaderAsset _softQuad;
	bool _ready = false;

	void SetUp() override {
		MaterialReadbackFixture::SetUp();
		if (!RenderSystem::IsRenderingAvailable()) {
			return;
		}
		SLT_CALL_LINKED_PACKAGE(VisualEffects, &_token);
		const std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/VisualEffects");
		_ready = !fixtures.empty() && _initialize.Import(Path((fixtures / "PositionInitialize.hlsl").generic_string())) &&
				 _update.Import(Path((fixtures / "AgeUpdate.hlsl").generic_string())) &&
				 _quad.Import(Path((fixtures / "QuadOutput.hlsl").generic_string())) &&
				 _softQuad.Import(Path((fixtures / "SoftQuadOutput.hlsl").generic_string()));
	}

	void TearDown() override {
		VisualEffectsImpl::ShutdownSimulation();
		FramePasses::UnregisterOwnedBy(&_token);
		FrameStages::UnregisterOwnedBy(&_token);
		MaterialReadbackFixture::TearDown();
	}

	bool Create(ParticleSystem& system, u32 capacity, const Vec3& position, MaterialAsset& material, f32 halfSize = 0.25f,
				VisualEffectsImpl::ParticleBatchKey key = {}, u32 slots = 1) {
		ParticleSystemDesc desc;
		desc.capacity = capacity;
		desc.attributeCount = 5;
		desc.initialize = Handle<ShaderAsset>(&_initialize);
		desc.update = Handle<ShaderAsset>(&_update);
		desc.outputs.push_back({Handle<MaterialAsset>(&material)});
		desc.batch = key;
		desc.batchSlots = slots;
		if (!system.Create(desc)) {
			return false;
		}
		f32 frame[32] = {};
		const f32 values[8] = {100.0f, position.x, position.y, position.z, halfSize, 0.5f, 0.0f, 0.0f};
		std::memcpy(frame + 24, values, sizeof(values));
		system.SetFrameData({reinterpret_cast<const u8*>(frame), sizeof(frame)});
		return true;
	}

	static void MakeMaterial(MaterialAsset& material, ShaderAsset& shader, const Color& tint,
							 std::optional<RHI::BlendMode> blend = std::nullopt) {
		material.shader = Handle<ShaderAsset>(&shader);
		material.SetColor("tint", tint);
		material.blend = blend;
	}

	Handle<ParticleProbeRenderer> AddEffect(Handle<Scene> scene) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Effect", scene);
		Handle<ParticleProbeRenderer> effect = object->AddComponent<ParticleProbeRenderer>();
		SceneSystem::EndFrame();
		return effect;
	}

	bool BuildEmptyStage(ShadingStage& stage, MaterialAsset& anyMaterial) {
		if (!BuildStage(&anyMaterial, stage)) {
			return false;
		}
		stage.quad->SetActive(false);
		SceneSystem::EndFrame();
		return true;
	}

	static f32 Channel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y, u32 channel) {
		return static_cast<f32>(pixels[(static_cast<size_t>(y) * size.x + x) * 4 + channel]);
	}

	static Vec3 TexelOnMiddleRow(const Vector<u8>& pixels, Vec2I size, const Vec3& world) {
		const ViewDesc view = QuadView();
		const Vec4 clip = view.projection * (view.view * Vec4(world.x, world.y, world.z, 1.0f));
		const i32 x = static_cast<i32>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<f32>(size.x));
		const size_t index = (static_cast<size_t>(size.y / 2) * size.x + static_cast<size_t>(std::clamp(x, 0, size.x - 1))) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	}
};

TEST_F(ParticleOutputFixture, ParticlesDrawWhereSpawned) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset green;
	MakeMaterial(green, _quad, Color(0.0f, 1.0f, 0.0f, 1.0f));
	ShadingStage stage;
	ASSERT_TRUE(BuildEmptyStage(stage, green));
	Handle<ParticleProbeRenderer> effect = AddEffect(stage.scene);
	ASSERT_TRUE(Create(effect->system, 16, Vec3(0.0f, 0.0f, 0.5f), green));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_EQ(effect->recorded, 1u);
	EXPECT_TRUE(effect->drew);
	EXPECT_LT(CenterTexel(pixels, size).y, 20.0f);

	effect->system.Advance(1.0f, 1);
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);
	EXPECT_LT(Channel(pixels, size, 2, 2, 1), 20.0f);

	TearDownStage(stage);
}

TEST_F(ParticleOutputFixture, OutputsDrawCounts) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset green;
	MakeMaterial(green, _quad, Color(0.0f, 1.0f, 0.0f, 1.0f));
	Asset* primitive = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)));
	ASSERT_NE(primitive, nullptr);
	EffectKernelHarness::LoadableMesh cube;
	cube.vertices = static_cast<MeshAsset*>(primitive)->vertices;
	cube.indices = static_cast<MeshAsset*>(primitive)->indices;
	AssetDatabase::UnloadAsset(primitive);
	cube.OnLoad();
	ASSERT_NE(cube.indexBufferAddress, 0u);
	const u32 indices = static_cast<u32>(cube.indices.size());
	ASSERT_GT(indices, 0u);
	ShadingStage stage;
	ASSERT_TRUE(BuildEmptyStage(stage, green));
	Handle<ParticleProbeRenderer> effect = AddEffect(stage.scene);
	effect->outputs = 3;

	ParticleSystemDesc desc;
	desc.capacity = 16;
	desc.attributeCount = 5;
	desc.initialize = Handle<ShaderAsset>(&_initialize);
	desc.update = Handle<ShaderAsset>(&_update);
	desc.outputs.push_back({Handle<MaterialAsset>(&green), 6});
	desc.outputs.push_back({Handle<MaterialAsset>(&green), 1});
	VisualEffectsImpl::ParticleOutputDesc meshed;
	meshed.material = Handle<MaterialAsset>(&green);
	meshed.instanced = true;
	meshed.mesh = 0;
	desc.outputs.push_back(meshed);
	desc.meshes.push_back({128, Handle<MeshAsset>(&cube)});
	ASSERT_TRUE(effect->system.Create(desc));
	f32 frame[40] = {};
	const f32 values[8] = {100.0f, 0.0f, 0.0f, 0.5f, 0.25f, 0.5f, 0.0f, 0.0f};
	std::memcpy(frame + 24, values, sizeof(values));
	effect->system.SetFrameData({reinterpret_cast<const u8*>(frame), sizeof(frame)});

	const auto draws = [&] {
		RHI::Device& device = *RenderSystem::GetDevice();
		device.WaitIdle();
		std::array<u32, 15> words{};
		device.ReadbackBuffer(effect->system.GetArgumentsBuffer(), {reinterpret_cast<u8*>(words.data()), sizeof(words)}, effect->system.GetArgumentsOffset());
		return words;
	};
	effect->system.Advance(1.0f, 3);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	std::array<u32, 15> words = draws();
	EXPECT_EQ(words[3], 18u);
	EXPECT_EQ(words[4], 1u);
	EXPECT_EQ(words[7], 3u);
	EXPECT_EQ(words[8], 1u);
	EXPECT_EQ(words[11], indices);
	EXPECT_EQ(words[12], 3u);
	EXPECT_EQ(effect->recorded, 3u);

	cube.OnUnload();
	effect->system.Advance(0.0f, 0);
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	words = draws();
	EXPECT_EQ(words[11], 0u);
	EXPECT_EQ(words[12], 3u);
	EXPECT_EQ(words[3], 18u);

	TearDownStage(stage);
}

TEST_F(ParticleOutputFixture, ParticleCompositesInOrder) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlit, nullptr);
	MaterialAsset* back = LoadMaterial("Back.mat", unlit->assetId,
									   "\t\"blend\": \"alpha\",\n\t\"colors\": { \"baseColor\": [1, 0, 0, 0.5] }");
	MaterialAsset* front = LoadMaterial("Front.mat", unlit->assetId,
										"\t\"blend\": \"alpha\",\n\t\"colors\": { \"baseColor\": [0, 0, 1, 0.5] }");
	ASSERT_NE(back, nullptr);
	ASSERT_NE(front, nullptr);
	MaterialAsset particle;
	MakeMaterial(particle, _quad, Color(0.0f, 1.0f, 0.0f, 0.5f), RHI::BlendMode::Alpha);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(back, stage));
	stage.sun->SetEnabled(false);
	ASSERT_TRUE(AddQuad(stage, front, 1.0f));
	Handle<ParticleProbeRenderer> effect = AddEffect(stage.scene);
	effect->localBounds = AABB::FromCenterExtents(Vec3(0.0f, 0.0f, 0.5f), Vec3(0.3f, 0.3f, 0.3f));
	ASSERT_TRUE(Create(effect->system, 4, Vec3(0.0f, 0.0f, 0.5f), particle));
	effect->system.Advance(1.0f, 1);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 center = CenterTexel(pixels, size);
	EXPECT_GT(center.z, center.y);
	EXPECT_GT(center.y, center.x) << center.x << ", " << center.y << ", " << center.z;

	TearDownStage(stage);
}

TEST_F(ParticleOutputFixture, TwoViewsDrawOneSimulation) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset green;
	MakeMaterial(green, _quad, Color(0.0f, 1.0f, 0.0f, 1.0f));
	ShadingStage stage;
	ASSERT_TRUE(BuildEmptyStage(stage, green));
	Handle<ParticleProbeRenderer> effect = AddEffect(stage.scene);
	ASSERT_TRUE(Create(effect->system, 16, Vec3(0.0f, 0.0f, 0.5f), green));
	const ViewTargetId second = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(second, INVALID_VIEW_TARGET);

	for (u32 frame = 0; frame < 3; ++frame) {
		effect->system.Advance(1.0f, 1);
		RenderSystem::SubmitView(stage.target, QuadView());
		RenderSystem::SubmitView(second, QuadView());
		RenderSystem::RenderAll();
	}
	EXPECT_EQ(effect->system.GetSimulatedFrames(), 3u);
	EXPECT_EQ(effect->recorded, 6u);
	EXPECT_EQ(RenderSystem::GetStats().proceduralDrawn, 2u);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	for (ViewTargetId target : {stage.target, second}) {
		ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
		EXPECT_GT(CenterTexel(pixels, size).y, 150.0f) << "view " << target;
	}

	RenderSystem::DestroyViewTarget(second);
	TearDownStage(stage);
}

TEST_F(ParticleOutputFixture, TheOutputBlendsAsItsMaterialSays) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset particle;
	MakeMaterial(particle, _quad, Color(0.6f, 0.0f, 0.0f, 0.5f));
	ShadingStage stage;
	ASSERT_TRUE(BuildEmptyStage(stage, particle));
	Handle<ParticleProbeRenderer> effect = AddEffect(stage.scene);
	ASSERT_TRUE(Create(effect->system, 4, Vec3(0.0f, 0.0f, 0.5f), particle));
	effect->system.Advance(1.0f, 1);

	const Color gray(0.2f, 0.2f, 0.2f, 1.0f);
	struct Read {
		f32 red = 0.0f;
		f32 green = 0.0f;
	};
	auto render = [&](std::optional<RHI::BlendMode> blend) {
		particle.blend = blend;
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		EXPECT_TRUE(RenderStage(stage, pixels, size, gray));
		const Vec3 center = CenterTexel(pixels, size);
		return Read{center.x, center.y};
	};
	const Read opaque = render(std::nullopt);
	const Read alpha = render(RHI::BlendMode::Alpha);
	const Read additive = render(RHI::BlendMode::Additive);
	const Read premultiplied = render(RHI::BlendMode::Premultiplied);

	EXPECT_LT(opaque.green, 10.0f);
	EXPECT_GT(alpha.green, opaque.green + 10.0f);
	EXPECT_GT(additive.green, alpha.green + 10.0f);
	EXPECT_GT(additive.red, alpha.red);
	EXPECT_GT(premultiplied.red, additive.red);

	TearDownStage(stage);
}

TEST_F(ParticleOutputFixture, SoftParticleFadesNearSurface) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlit, nullptr);
	MaterialAsset* wall = LoadMaterial("Wall.mat", unlit->assetId, "\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
	ASSERT_NE(wall, nullptr);
	MaterialAsset soft;
	MakeMaterial(soft, _softQuad, Color(0.0f, 1.0f, 0.0f, 1.0f));

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(wall, stage));
	stage.sun->SetEnabled(false);
	Handle<ParticleProbeRenderer> nearWall = AddEffect(stage.scene);
	Handle<ParticleProbeRenderer> clear = AddEffect(stage.scene);
	ASSERT_TRUE(Create(nearWall->system, 4, Vec3(-0.25f, 0.0f, 0.05f), soft, 0.12f));
	ASSERT_TRUE(Create(clear->system, 4, Vec3(0.25f, 0.0f, 1.5f), soft, 0.06f));
	nearWall->system.Advance(1.0f, 1);
	clear->system.Advance(1.0f, 1);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	auto greenAt = [&](const Vec3& world) {
		const Vec4 clip = QuadView().projection * (QuadView().view * Vec4(world.x, world.y, world.z, 1.0f));
		const i32 x = static_cast<i32>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<f32>(size.x));
		const i32 y = static_cast<i32>((0.5f - clip.y / clip.w * 0.5f) * static_cast<f32>(size.y));
		return Channel(pixels, size, x, y, 1);
	};
	const f32 faded = greenAt(Vec3(-0.25f, 0.0f, 0.05f));
	const f32 whole = greenAt(Vec3(0.25f, 0.0f, 1.5f));
	EXPECT_GT(whole, 150.0f);
	EXPECT_LT(faded, whole * 0.5f) << faded << " against " << whole;

	TearDownStage(stage);
}

TEST_F(ParticleOutputFixture, BatchedSystemsDrawSeparately) {
	if (!_ready) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset green;
	MakeMaterial(green, _quad, Color(0.0f, 1.0f, 0.0f, 1.0f));
	MaterialAsset red;
	MakeMaterial(red, _quad, Color(1.0f, 0.0f, 0.0f, 1.0f));
	ShadingStage stage;
	ASSERT_TRUE(BuildEmptyStage(stage, green));
	constexpr VisualEffectsImpl::ParticleBatchKey SPARKS{7, 1, 0};
	const Vec3 left(-0.6f, 0.0f, 0.5f);
	const Vec3 right(0.6f, 0.0f, 0.5f);
	const Vec3 above(0.0f, 5.0f, 0.5f);
	Handle<ParticleProbeRenderer> greenEffect = AddEffect(stage.scene);
	Handle<ParticleProbeRenderer> redEffect = AddEffect(stage.scene);
	ASSERT_TRUE(Create(greenEffect->system, 16, left, green, 0.2f, SPARKS, 4));
	ASSERT_TRUE(Create(redEffect->system, 16, above, red, 0.2f, SPARKS, 4));
	ASSERT_EQ(greenEffect->system.GetBatch(), redEffect->system.GetBatch());
	EXPECT_EQ(redEffect->system.GetSlot(), 1u);
	const auto spawnAt = [](ParticleSystem& system, const Vec3& at, f32 lifetime) {
		f32 frame[32] = {};
		const f32 values[8] = {lifetime, at.x, at.y, at.z, 0.2f, 0.5f, 0.0f, 0.0f};
		std::memcpy(frame + 24, values, sizeof(values));
		system.SetFrameData({reinterpret_cast<const u8*>(frame), sizeof(frame)});
	};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};

	spawnAt(redEffect->system, above, 1.5f);
	redEffect->system.Advance(1.0f, 1);
	greenEffect->system.Advance(1.0f, 0);
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	spawnAt(redEffect->system, right, 100.0f);
	redEffect->system.Advance(1.0f, 1);
	greenEffect->system.Advance(1.0f, 0);
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_GT(TexelOnMiddleRow(pixels, size, right).x, 150.0f);
	EXPECT_LT(TexelOnMiddleRow(pixels, size, left).y, 20.0f);
	redEffect->system.Advance(1.0f, 0);
	greenEffect->system.Advance(1.0f, 1);
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_GT(TexelOnMiddleRow(pixels, size, right).x, 150.0f);
	EXPECT_GT(TexelOnMiddleRow(pixels, size, left).y, 150.0f);
	redEffect->system.Advance(1.0f, 0);
	greenEffect->system.Advance(1.0f, 1);
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 drawnLeft = TexelOnMiddleRow(pixels, size, left);
	const Vec3 drawnRight = TexelOnMiddleRow(pixels, size, right);
	EXPECT_GT(drawnRight.x, 150.0f);
	EXPECT_LT(drawnRight.y, 20.0f);
	EXPECT_GT(drawnLeft.y, 150.0f);
	EXPECT_LT(drawnLeft.x, 20.0f);
	EXPECT_LT(CenterTexel(pixels, size).x + CenterTexel(pixels, size).y, 20.0f);

	greenEffect->system.Release();
	redEffect->system.Advance(1.0f, 0);
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_LT(TexelOnMiddleRow(pixels, size, left).y, 20.0f);
	EXPECT_GT(TexelOnMiddleRow(pixels, size, right).x, 150.0f);

	TearDownStage(stage);
}

#endif
