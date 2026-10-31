#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include <filesystem>
#include <fstream>
#include <iterator>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* SPRITE_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#include \"ViewData.hlsli\"\n"
		"[[vk::binding(0, 2)]] cbuffer sltViewRecord { ShalltearViewData sltPassView; };\n"
		"struct SpritePush { float3 center; float halfSize; float4 color; };\n"
		"[[vk::push_constant]] SpritePush gSprite;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tfloat x = (vertexId == 1 || vertexId == 3 || vertexId == 4) ? 1.0 : -1.0;\n"
		"\tfloat y = (vertexId == 2 || vertexId == 4 || vertexId == 5) ? 1.0 : -1.0;\n"
		"\tfloat4x4 view = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);\n"
		"\tfloat4x4 projection = ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1],\n"
		"\t\tsltPassView.projection[2], sltPassView.projection[3]);\n"
		"\tfloat3 world = gSprite.center + float3(x, y, 0.0) * gSprite.halfSize;\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(projection, mul(view, float4(world, 1.0)));\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return gSprite.color; }\n";

	constexpr const char* DEPTH_READING_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#include \"ViewData.hlsli\"\n"
		"[[vk::binding(0, 2)]] cbuffer sltViewRecord { ShalltearViewData sltPassView; };\n"
		"[[vk::binding(1, 2)]] Texture2D sltSceneDepth;\n"
		"struct SpritePush { float3 center; float halfSize; float4 color; };\n"
		"[[vk::push_constant]] SpritePush gSprite;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tfloat x = (vertexId == 1 || vertexId == 3 || vertexId == 4) ? 1.0 : -1.0;\n"
		"\tfloat y = (vertexId == 2 || vertexId == 4 || vertexId == 5) ? 1.0 : -1.0;\n"
		"\tfloat4x4 view = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);\n"
		"\tfloat4x4 projection = ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1],\n"
		"\t\tsltPassView.projection[2], sltPassView.projection[3]);\n"
		"\tfloat3 world = gSprite.center + float3(x, y, 0.0) * gSprite.halfSize;\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(projection, mul(view, float4(world, 1.0)));\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\tfloat depth = sltSceneDepth.Load(int3(int2(input.positionCS.xy), 0)).r;\n"
		"\treturn float4(depth * 20.0, 0.0, 0.0, 1.0);\n"
		"}\n";

	constexpr const char* POINT_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#pragma shalltear_topology point\n"
		"#include \"ViewData.hlsli\"\n"
		"[[vk::binding(0, 2)]] cbuffer sltViewRecord { ShalltearViewData sltPassView; };\n"
		"struct SpritePush { float3 center; float halfSize; float4 color; };\n"
		"[[vk::push_constant]] SpritePush gSprite;\n"
		"struct VSOutput { float4 positionCS : SV_Position; [[vk::builtin(\"PointSize\")]] float pointSize : PSIZE; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tfloat4x4 view = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);\n"
		"\tfloat4x4 projection = ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1],\n"
		"\t\tsltPassView.projection[2], sltPassView.projection[3]);\n"
		"\tfloat3 world = gSprite.center + float3(float(vertexId) * gSprite.halfSize, 0.0, 0.0);\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(projection, mul(view, float4(world, 1.0)));\n"
		"\toutput.pointSize = 1.0;\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(float4 positionCS : SV_Position) : SV_Target { return gSprite.color; }\n";

	constexpr const char* LIT_SPRITE_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#include \"Shading.hlsli\"\n"
		"struct SpritePush { float3 center; float halfSize; float4 color; };\n"
		"[[vk::push_constant]] SpritePush gSprite;\n"
		"struct VSOutput { float4 positionCS : SV_Position; float3 positionWS : TEXCOORD0; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tfloat x = (vertexId == 1 || vertexId == 3 || vertexId == 4) ? 1.0 : -1.0;\n"
		"\tfloat y = (vertexId == 2 || vertexId == 4 || vertexId == 5) ? 1.0 : -1.0;\n"
		"\tVSOutput output;\n"
		"\toutput.positionWS = gSprite.center + float3(x, y, 0.0) * gSprite.halfSize;\n"
		"\toutput.positionCS = mul(sltViewProjection, float4(output.positionWS, 1.0));\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\tShalltearSurfaceInput surfaceInput = (ShalltearSurfaceInput)0;\n"
		"\tsurfaceInput.positionWS = input.positionWS;\n"
		"\tsurfaceInput.normalWS = float3(0.0, 0.0, 1.0);\n"
		"\tsurfaceInput.viewDirectionWS = ShalltearViewDirection(input.positionWS);\n"
		"\tsurfaceInput.positionSS = input.positionCS.xy;\n"
		"\tShalltearSurface surface = ShalltearDefaultSurface();\n"
		"\tsurface.baseColor = gSprite.color.rgb;\n"
		"\tsurface.roughness = 0.5;\n"
		"\treturn float4(ShalltearShadeSurface(surfaceInput, surface, float3(0.0, 0.0, 1.0), 1.0), 1.0);\n"
		"}\n";

	constexpr const char* CASTER_SPRITE_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull off\n"
		"#pragma shalltear_depth_program\n"
		"#include \"ViewData.hlsli\"\n"
		"[[vk::binding(0, 2)]] cbuffer sltViewRecord { ShalltearViewData sltPassView; };\n"
		"struct SpritePush { float3 center; float halfSize; float4 color; };\n"
		"[[vk::push_constant]] SpritePush gSprite;\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tfloat x = (vertexId == 1 || vertexId == 3 || vertexId == 4) ? 1.0 : -1.0;\n"
		"\tfloat y = (vertexId == 2 || vertexId == 4 || vertexId == 5) ? 1.0 : -1.0;\n"
		"\tfloat4x4 view = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);\n"
		"\tfloat4x4 projection = ShalltearViewDataMatrix(sltPassView.projection[0], sltPassView.projection[1],\n"
		"\t\tsltPassView.projection[2], sltPassView.projection[3]);\n"
		"\tfloat3 world = gSprite.center + float3(x, y, 0.0) * gSprite.halfSize;\n"
		"\tVSOutput output;\n"
		"\toutput.positionCS = mul(projection, mul(view, float4(world, 1.0)));\n"
		"\treturn output;\n"
		"}\n"
		"#ifdef SHALLTEAR_DEPTH_PROGRAM\n"
		"float4 PSMain(VSOutput input) : SV_Target0 { return float4(0.5, 0.5, 1.0, 1.0); }\n"
		"#else\n"
		"float4 PSMain(VSOutput input) : SV_Target { return gSprite.color; }\n"
		"#endif\n";

	constexpr const char* SHADOW_TALLY_SHADER =
		"#define SHALLTEAR_CUSTOM_LIGHTING\n"
		"#pragma shalltear_template lit\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
		"float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n"
		"\treturn float3(light.shadow, 0.0, 0.25);\n"
		"}\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = float3(0.0, 0.0, 0.0);\n"
		"\tsurface.reflectance = 0.0;\n"
		"}\n";

	struct SpritePush {
		Vec3 center{};
		f32 halfSize = 0.5f;
		Color color = Color::White();
	};
	static_assert(sizeof(SpritePush) == 32);

	struct KernelPush {
		u32 mode = 0;
		u32 count = 0;
		u32 base = 0;
		u32 spin = 0;
		u32 mask = 0;
	};

	class TestSprite : public ProceduralRenderer {
	public:
		SLT_REFLECT_COMPONENT(TestSprite, ProceduralRenderer)
		SLT_END_REFLECT()

		Handle<MaterialAsset> material;
		f32 halfSize = 0.5f;
		Color color = Color::White();
		bool bounded = true;
		u32 vertices = 6;
		RHI::BufferHandle arguments;
		RHI::BufferHandle stray;
		u32 declared = 0;
		u32 recorded = 0;
		bool drew = false;
		Handle<ShaderAsset> viewKernel;
		f32 nearEye = 0.0f;
		RHI::BufferHandle viewStray;
		bool writeless = false;
		u32 viewRecorded = 0;
		bool viewDispatched = false;
		bool castShadows = false;
		u32 prepassRecorded = 0;
		u32 shadowRecorded = 0;
		Mat4 shadowView = Mat4::Identity();

		Vec3 Center() const {
			const SceneObject* owner = GetOwner().Get();
			return owner ? owner->GetWorldPosition() : Vec3{};
		}

		void Declare(ProceduralPlan& plan) override {
			++declared;
			if (bounded) {
				plan.SetBounds(AABB::FromCenterExtents(Center(), Vec3(halfSize, halfSize, halfSize)));
			}
			if (viewKernel.IsValid()) {
				plan.AddViewStep();
				if (!writeless) {
					plan.Write(arguments, RHI::ResourceState::UnorderedAccess);
				}
			}
			plan.AddItem(material, Center(), castShadows);
			if (arguments.IsValid()) {
				plan.Read(arguments, RHI::ResourceState::IndirectArgument);
			}
		}

		void RecordView(ComputeContext& context, const ProceduralView& view) override {
			++viewRecorded;
			KernelPush push;
			push.mode = 3;
			push.count = view.cameraPosition.z > nearEye ? 6u : 0u;
			const ComputeBinding binding{0, viewStray.IsValid() ? viewStray : arguments};
			viewDispatched = context.Dispatch(viewKernel, {&binding, 1}, {}, {reinterpret_cast<const u8*>(&push), sizeof(push)}, 1);
		}

		void Record(ProceduralContext& context) override {
			if (context.GetPass() == ProceduralPass::Shadow) {
				++shadowRecorded;
				shadowView = context.GetView();
			} else if (context.GetPass() == ProceduralPass::Prepass) {
				++prepassRecorded;
			} else {
				++recorded;
			}
			SpritePush push;
			push.center = Center();
			push.halfSize = halfSize;
			push.color = color;
			const std::span<const u8> bytes{reinterpret_cast<const u8*>(&push), sizeof(push)};
			if (stray.IsValid()) {
				drew = context.DrawIndirect(bytes, stray);
			} else {
				drew = arguments.IsValid() ? context.DrawIndirect(bytes, arguments) : context.Draw(bytes, vertices);
			}
		}
	};

	SLT_REGISTER_TYPE(TestSprite)

	class ArgumentsPass final : public FramePass {
	public:
		Handle<ShaderAsset> kernel;
		Vector<RHI::BufferHandle> buffers{GetMemoryResource(MemTag::Graphics)};
		Vector<u32> vertices{GetMemoryResource(MemTag::Graphics)};

		void Declare(FramePassPlan& plan) override {
			for (RHI::BufferHandle buffer : buffers) {
				plan.AddStep();
				plan.Write(buffer, RHI::ResourceState::UnorderedAccess);
			}
		}

		void Record(ComputeContext& context) override {
			const u32 step = context.GetStep();
			KernelPush push;
			push.mode = 3;
			push.count = vertices[step];
			const ComputeBinding binding{0, buffers[step]};
			context.Dispatch(kernel, {&binding, 1}, {}, {reinterpret_cast<const u8*>(&push), sizeof(push)}, 1);
		}
	};

	class DepthKeepPass final : public FramePass {
	public:
		Handle<ShaderAsset> kernel;
		Handle<Camera> camera;
		RHI::BufferHandle values;
		CameraDepth answer;

		void Declare(FramePassPlan& plan) override {
			plan.AddStep();
			answer = plan.ReadCameraDepth(camera);
			plan.Write(values, RHI::ResourceState::UnorderedAccess);
		}

		void Record(ComputeContext& context) override {
			if (!answer.image.IsValid()) {
				return;
			}
			const u32 pixel[2] = {static_cast<u32>(answer.size.x / 2), static_cast<u32>(answer.size.y / 2)};
			const ComputeBinding binding{0, values};
			const ComputeTexture texture{1, 0, {}, answer.image};
			context.Dispatch(kernel, {&binding, 1}, {&texture, 1}, {reinterpret_cast<const u8*>(pixel), sizeof(pixel)}, 1);
		}
	};

	char ARGUMENTS_PASS_OWNER = 0;

} // anonymous namespace

class ProceduralRendererFixture : public MaterialReadbackFixture {
protected:
	void TearDown() override {
		FramePasses::UnregisterOwnedBy(&ARGUMENTS_PASS_OWNER);
		MaterialReadbackFixture::TearDown();
	}

	bool Ready() { return RenderSystem::IsRenderingAvailable() && CompilerAvailable(); }

	MaterialAsset* LoadShaderMaterial(const char* materialFile, const char* shaderFile, const char* shaderSource, const str& body) {
		WriteProjectFile(shaderFile, shaderSource);
		Asset* shader = AssetDatabase::LoadAsset(Path(str("project://") + shaderFile));
		if (!shader) {
			return nullptr;
		}
		return LoadMaterial(materialFile, shader->assetId, body);
	}

	MaterialAsset* LoadUnlit(const char* file, const Color& color, bool blended) {
		Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
		if (!unlit) {
			return nullptr;
		}
		str body = blended ? "\t\"blend\": \"alpha\",\n" : "";
		body += "\t\"colors\": { \"baseColor\": [" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " +
				std::to_string(color.b) + ", " + std::to_string(color.a) + "] }";
		return LoadMaterial(file, unlit->assetId, body);
	}

	ShaderAsset* LoadComputeFixture() { return LoadKernelFixture("ComputeFixture.hlsl"); }

	ShaderAsset* LoadKernelFixture(const char* name) {
		const std::filesystem::path source = RepoDir((str("Source/Tests/Fixtures/Graphics/") + name).c_str());
		if (source.empty()) {
			return nullptr;
		}
		std::ifstream file(source);
		const str text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		WriteProjectFile(name, text);
		auto* kernel = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + name)));
		return kernel && kernel->IsKernel() ? kernel : nullptr;
	}

	static RHI::BufferHandle CreateArguments(RHI::Device& device) {
		RHI::BufferDesc desc;
		desc.size = 16;
		desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect;
		desc.debugName = "TestSpriteArguments";
		return device.CreateBufferResource(desc);
	}

	Handle<TestSprite> AddSprite(Handle<Scene> scene, MaterialAsset* material, const Vec3& position, const Color& color) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Sprite", scene);
		object->SetLocalPosition(position);
		Handle<TestSprite> sprite = object->AddComponent<TestSprite>();
		sprite->material = Handle<MaterialAsset>(material);
		sprite->color = color;
		SceneSystem::EndFrame();
		return sprite;
	}

	static Color32 TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return Color32{pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]};
	}
};

TEST_F(ProceduralRendererFixture, BlendedItemsCompositeInOrder) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* back = LoadUnlit("Back.mat", Color(1.0f, 0.0f, 0.0f, 0.5f), true);
	MaterialAsset* front = LoadUnlit("Front.mat", Color(0.0f, 0.0f, 1.0f, 0.5f), true);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "\t\"blend\": \"alpha\"");
	ASSERT_NE(back, nullptr);
	ASSERT_NE(front, nullptr);
	ASSERT_NE(item, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(back, stage));
	stage.sun->SetEnabled(false);
	ASSERT_TRUE(AddQuad(stage, front, 1.0f));
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(0.0f, 0.0f, 0.5f), Color(0.0f, 1.0f, 0.0f, 0.5f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ASSERT_EQ(sprite->recorded, 1u);
	EXPECT_TRUE(sprite->drew);

	const Vec3 center = CenterTexel(pixels, size);
	EXPECT_GT(center.z, center.y);
	EXPECT_GT(center.y, center.x) << center.x << ", " << center.y << ", " << center.z;
	EXPECT_GT(center.x, 0.0f);

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, BlendedItemsSortBackToFront) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* nearItem = LoadShaderMaterial("Near.mat", "Sprite.hlsl", SPRITE_SHADER, "\t\"blend\": \"alpha\"");
	Asset* sprite = AssetDatabase::LoadAsset(Path("project://Sprite.hlsl"));
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(nearItem, nullptr);
	ASSERT_NE(sprite, nullptr);
	MaterialAsset* farItem = LoadMaterial("Far.mat", sprite->assetId, "\t\"blend\": \"alpha\"");
	ASSERT_NE(farItem, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);
	AddSprite(stage.scene, nearItem, Vec3(0.0f, 0.0f, 0.5f), Color(1.0f, 0.0f, 0.0f, 0.5f));
	AddSprite(stage.scene, farItem, Vec3(0.0f, 0.0f, 0.0f), Color(0.0f, 0.0f, 1.0f, 0.5f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	Vec3 center = CenterTexel(pixels, size);
	EXPECT_GT(center.x, center.z) << center.x << ", " << center.y << ", " << center.z;

	farItem->sortingPriority = 1;
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	center = CenterTexel(pixels, size);
	EXPECT_GT(center.z, center.x) << center.x << ", " << center.y << ", " << center.z;

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, BlendedItemOverSky) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "\t\"blend\": \"alpha\"");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);
	Handle<SceneObject> environmentObject = SceneSystem::CreateObject("Environment", stage.scene);
	Handle<Environment> environment = environmentObject->AddComponent<Environment>();
	environment->SetSkyMode(SkyMode::Color);
	environment->SetSkyColor(Color(0.9f, 0.0f, 0.0f, 1.0f));
	SceneSystem::EndFrame();
	AddSprite(stage.scene, item, Vec3(0.0f, 0.0f, 0.5f), Color(0.0f, 1.0f, 0.0f, 0.5f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Color32 corner = TexelAt(pixels, size, 2, 2);
	const Vec3 center = CenterTexel(pixels, size);
	ASSERT_GT(corner.r, 150);
	EXPECT_GT(center.y, 60.0f);
	EXPECT_LT(center.x, static_cast<f32>(corner.r) * 0.85f);

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, PointListShaderDrawsOneTexelVertex) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Points.mat", "Points.hlsl", POINT_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);

	const f32 texel = 2.0f * 3.0f * std::tan(Math::DegToRad(30.0f)) / 64.0f;
	Handle<TestSprite> points = AddSprite(stage.scene, item, Vec3(0.5f * texel, -0.5f * texel, 0.0f), Color::Green());
	points->halfSize = 8.0f * texel;
	points->vertices = 3;
	points->bounded = false;
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ASSERT_EQ(size, Vec2I(64, 64));
	EXPECT_TRUE(points->drew);
	u32 lit = 0;
	for (i32 y = 0; y < size.y; ++y) {
		for (i32 x = 0; x < size.x; ++x) {
			if (TexelAt(pixels, size, x, y).g > 128) {
				++lit;
				EXPECT_TRUE(y == 32 && (x == 31 || x == 23 || x == 15)) << "texel (" << x << ", " << y << ") is lit";
			}
		}
	}
	EXPECT_EQ(lit, 3u);

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, CulledRendererRecordsNothing) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);

	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(50.0f, 0.0f, 0.0f), Color::Green());
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	GraphicsStats stats = RenderSystem::GetStats();
	EXPECT_EQ(sprite->declared, 1u);
	EXPECT_EQ(sprite->recorded, 0u);
	EXPECT_EQ(stats.proceduralCulled, 1u);
	EXPECT_EQ(stats.proceduralDrawn, 0u);

	sprite->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, 0.5f));
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	stats = RenderSystem::GetStats();
	EXPECT_EQ(sprite->recorded, 1u);
	EXPECT_EQ(stats.proceduralCulled, 0u);
	EXPECT_EQ(stats.proceduralDrawn, 1u);
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);

	sprite->GetOwner()->SetLocalPosition(Vec3(50.0f, 0.0f, 0.0f));
	sprite->bounded = false;
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	stats = RenderSystem::GetStats();
	EXPECT_EQ(sprite->recorded, 2u);
	EXPECT_EQ(stats.proceduralCulled, 0u);

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, RendererKnowsWhetherLastFrameCulledIt) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);
	const ViewTargetId second = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(second, INVALID_VIEW_TARGET);
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(50.0f, 0.0f, 0.0f), Color::Green());
	EXPECT_FALSE(sprite->IsCulled());
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};

	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_TRUE(sprite->IsCulled());
	sprite->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, 0.5f));
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_FALSE(sprite->IsCulled());

	sprite->GetOwner()->SetLocalPosition(Vec3(50.0f, 0.0f, 0.0f));
	ViewDesc beside = QuadView();
	beside.view = Mat4::LookAt(Vec3(50.0f, 0.0f, 3.0f), Vec3(50.0f, 0.0f, 0.0f));
	beside.cameraPosition = Vec3(50.0f, 0.0f, 3.0f);
	RenderSystem::SubmitView(stage.target, QuadView());
	RenderSystem::SubmitView(second, beside);
	RenderSystem::RenderAll();
	EXPECT_EQ(RenderSystem::GetStats().proceduralCulled, 1u);
	EXPECT_FALSE(sprite->IsCulled());

	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ASSERT_TRUE(sprite->IsCulled());
	ViewDesc leaving = QuadView();
	const StringHash named[] = {TestSprite::GetReflectType()->name};
	leaving.skippedProceduralTypes = {named, 1};
	RenderSystem::SubmitView(stage.target, leaving);
	RenderSystem::RenderAll();
	EXPECT_FALSE(sprite->IsCulled());

	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ASSERT_TRUE(sprite->IsCulled());
	sprite->SetEnabled(false);
	sprite->SetEnabled(true);
	EXPECT_FALSE(sprite->IsCulled());
	sprite->bounded = false;
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_FALSE(sprite->IsCulled());

	RenderSystem::DestroyViewTarget(second);
	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, DepthReaderEnablesPrepass) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* wall = LoadUnlit("Wall.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Depth.mat", "Depth.hlsl", DEPTH_READING_SHADER, "");
	ASSERT_NE(wall, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(wall, stage));
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(0.0f, 0.0f, 0.5f), Color::White());
	sprite->halfSize = 0.25f;

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ASSERT_EQ(sprite->recorded, 1u);
	EXPECT_GT(CenterTexel(pixels, size).x, 100.0f);

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, TemplatedShaderItemRefused) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* templated = LoadUnlit("Templated.mat", Color(0.0f, 1.0f, 0.0f, 1.0f), false);
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(templated, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);
	Handle<TestSprite> sprite = AddSprite(stage.scene, templated, Vec3(0.0f, 0.0f, 0.5f), Color::Green());

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_EQ(sprite->recorded, 0u);
	EXPECT_LT(CenterTexel(pixels, size).y, 20.0f);

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, IndirectItemsDrawKernelCounts) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShaderAsset* kernel = nullptr;
	{
		const std::filesystem::path source = RepoDir("Source/Tests/Fixtures/Graphics/ComputeFixture.hlsl");
		ASSERT_FALSE(source.empty());
		std::ifstream file(source);
		const str text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		WriteProjectFile("ComputeFixture.hlsl", text);
		kernel = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("project://ComputeFixture.hlsl")));
		ASSERT_NE(kernel, nullptr);
		ASSERT_TRUE(kernel->IsKernel());
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);

	RHI::Device& device = *RenderSystem::GetDevice();
	ArgumentsPass arguments;
	arguments.kernel = Handle<ShaderAsset>(kernel);
	constexpr u32 SPRITES = 4;
	const f32 x[SPRITES] = {-0.75f, -0.25f, 0.25f, 0.75f};
	const u32 vertices[SPRITES] = {6, 0, 6, 0};
	Handle<TestSprite> sprites[SPRITES];
	for (u32 i = 0; i < SPRITES; ++i) {
		RHI::BufferDesc desc;
		desc.size = 16;
		desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect;
		desc.debugName = "TestSpriteArguments";
		arguments.buffers.push_back(device.CreateBufferResource(desc));
		arguments.vertices.push_back(vertices[i]);
		sprites[i] = AddSprite(stage.scene, item, Vec3(x[i], 0.0f, 0.5f), Color::Green());
		sprites[i]->halfSize = 0.2f;
		sprites[i]->arguments = arguments.buffers.back();
	}
	ASSERT_TRUE(FramePasses::Register(arguments, &ARGUMENTS_PASS_OWNER));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	for (u32 i = 0; i < SPRITES; ++i) {
		EXPECT_TRUE(sprites[i]->drew) << "sprite " << i;
		const Vec4 clip = QuadView().projection * (QuadView().view * Vec4(x[i], 0.0f, 0.5f, 1.0f));
		const i32 px = static_cast<i32>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<f32>(size.x));
		const u8 green = TexelAt(pixels, size, px, size.y / 2).g;
		if (vertices[i] > 0) {
			EXPECT_GT(green, 150) << "sprite " << i;
		} else {
			EXPECT_LT(green, 20) << "sprite " << i;
		}
	}

	FramePasses::UnregisterOwnedBy(&ARGUMENTS_PASS_OWNER);
	TearDownStage(stage);
	for (RHI::BufferHandle buffer : arguments.buffers) {
		device.DestroyResource(buffer);
	}
}

TEST_F(ProceduralRendererFixture, IndirectDrawFromUndeclaredBufferIsRefused) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);

	RHI::Device& device = *RenderSystem::GetDevice();
	RHI::BufferDesc desc;
	desc.size = 16;
	desc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::Indirect;
	RHI::BufferHandle stray = device.CreateBufferResource(desc);
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(0.0f, 0.0f, 0.5f), Color::Green());
	sprite->stray = stray;

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_EQ(sprite->recorded, 1u);
	EXPECT_FALSE(sprite->drew);

	TearDownStage(stage);
	device.DestroyResource(stray);
}

TEST_F(ProceduralRendererFixture, ViewsShareOneDeclare) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(0.0f, 0.0f, 0.5f), Color::Green());

	const ViewTargetId second = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(second, INVALID_VIEW_TARGET);
	RenderSystem::SubmitView(stage.target, QuadView());
	RenderSystem::SubmitView(second, QuadView());
	RenderSystem::RenderAll();
	const GraphicsStats stats = RenderSystem::GetStats();
	EXPECT_EQ(sprite->declared, 1u);
	EXPECT_EQ(sprite->recorded, 2u);
	EXPECT_EQ(stats.proceduralDrawn, 2u);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(second, pixels, size));
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);

	RenderSystem::DestroyViewTarget(second);
	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, ViewStepsShareBuffer) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShaderAsset* kernel = LoadComputeFixture();
	ASSERT_NE(kernel, nullptr);
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);

	RHI::Device& device = *RenderSystem::GetDevice();
	const RHI::BufferHandle arguments = CreateArguments(device);
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(0.0f, 0.0f, 0.5f), Color::Green());
	sprite->arguments = arguments;
	sprite->viewKernel = Handle<ShaderAsset>(kernel);
	sprite->nearEye = 2.75f;

	const ViewTargetId nearTarget = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(nearTarget, INVALID_VIEW_TARGET);
	ViewDesc nearView = QuadView();
	nearView.cameraPosition = Vec3(0.0f, 0.0f, 2.5f);
	nearView.view = Mat4::LookAt(nearView.cameraPosition, Vec3(0.0f, 0.0f, 0.0f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	for (const bool farFirst : {true, false}) {
		const u32 before = sprite->viewRecorded;
		if (farFirst) {
			RenderSystem::SubmitView(stage.target, QuadView());
			RenderSystem::SubmitView(nearTarget, nearView);
		} else {
			RenderSystem::SubmitView(nearTarget, nearView);
			RenderSystem::SubmitView(stage.target, QuadView());
		}
		RenderSystem::RenderAll();
		EXPECT_EQ(sprite->viewRecorded - before, 2u);
		EXPECT_TRUE(sprite->viewDispatched);
		ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));
		EXPECT_GT(CenterTexel(pixels, size).y, 150.0f) << farFirst;
		ASSERT_TRUE(RenderSystem::ReadbackViewTarget(nearTarget, pixels, size));
		EXPECT_LT(CenterTexel(pixels, size).y, 20.0f) << farFirst;
	}
	EXPECT_EQ(sprite->declared, 2u);

	RenderSystem::DestroyViewTarget(nearTarget);
	TearDownStage(stage);
	device.DestroyResource(arguments);
}

TEST_F(ProceduralRendererFixture, ViewStepRunsWhereDrawn) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShaderAsset* kernel = LoadComputeFixture();
	ASSERT_NE(kernel, nullptr);
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);

	RHI::Device& device = *RenderSystem::GetDevice();
	const RHI::BufferHandle arguments = CreateArguments(device);
	const RHI::BufferHandle stray = CreateArguments(device);
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(50.0f, 0.0f, 0.0f), Color::Green());
	sprite->arguments = arguments;
	sprite->viewKernel = Handle<ShaderAsset>(kernel);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_EQ(sprite->viewRecorded, 0u);

	sprite->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, 0.5f));
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_EQ(sprite->viewRecorded, 1u);
	EXPECT_TRUE(sprite->viewDispatched);
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);

	sprite->viewStray = stray;
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_EQ(sprite->viewRecorded, 2u);
	EXPECT_FALSE(sprite->viewDispatched);

	sprite->viewStray = {};
	sprite->writeless = true;
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_EQ(sprite->viewRecorded, 2u);
	EXPECT_EQ(sprite->recorded, 3u);

	TearDownStage(stage);
	device.DestroyResource(arguments);
	device.DestroyResource(stray);
}

TEST_F(ProceduralRendererFixture, ViewExcludesNamedTypes) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* hidden = LoadUnlit("Hidden.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(hidden, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(hidden, stage));
	stage.quad->SetActive(false);
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(0.0f, 0.0f, 0.5f), Color::Green());
	const ViewTargetId second = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(second, INVALID_VIEW_TARGET);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};

	ViewDesc leaving = QuadView();
	const StringHash named[] = {TestSprite::GetReflectType()->name};
	leaving.skippedProceduralTypes = {named, 1};
	RenderSystem::SubmitView(stage.target, leaving);
	RenderSystem::SubmitView(second, QuadView());
	RenderSystem::RenderAll();
	const GraphicsStats stats = RenderSystem::GetStats();
	EXPECT_EQ(sprite->recorded, 1u);
	EXPECT_EQ(stats.proceduralDrawn, 1u);
	EXPECT_EQ(stats.proceduralCulled, 0u);
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));
	EXPECT_LT(CenterTexel(pixels, size).y, 20.0f);
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(second, pixels, size));
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);

	ViewDesc byBase = QuadView();
	const StringHash base[] = {ProceduralRenderer::GetReflectType()->name};
	byBase.skippedProceduralTypes = {base, 1};
	RenderSystem::SubmitView(second, byBase);
	RenderSystem::RenderAll();
	EXPECT_EQ(sprite->recorded, 1u);
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(second, pixels, size));
	EXPECT_LT(CenterTexel(pixels, size).y, 20.0f);

	RenderSystem::DestroyViewTarget(second);
	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, IsolatedRendererDrawsInItsView) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* item = LoadShaderMaterial("Sprite.mat", "Sprite.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(item, nullptr);
	Handle<Scene> preview = SceneSystem::CreateIsolatedScene("Preview");
	Handle<TestSprite> sprite = AddSprite(preview, item, Vec3(0.0f, 0.0f, 0.5f), Color::Green());
	const ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderSystem::SubmitView(target, QuadView());
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_EQ(sprite->recorded, 0u);
	EXPECT_LT(CenterTexel(pixels, size).y, 20.0f);

	ViewDesc named = QuadView();
	const Handle<Scene> only[] = {preview};
	named.scenes = {only, 1};
	RenderSystem::SubmitView(target, named);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	EXPECT_EQ(sprite->recorded, 1u);
	EXPECT_GT(CenterTexel(pixels, size).y, 150.0f);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(preview);
	SceneSystem::EndFrame();
}

TEST_F(ProceduralRendererFixture, LitItemShadesLikeLitQuad) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	Asset* lit = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(lit, nullptr);
	MaterialAsset* quadMaterial = LoadMaterial("LitQuad.mat", lit->assetId, "\t\"colors\": { \"baseColor\": [0.2, 0.6, 0.9, 1.0] }");
	MaterialAsset* item = LoadShaderMaterial("LitSprite.mat", "LitSprite.hlsl", LIT_SPRITE_SHADER, "");
	ASSERT_NE(quadMaterial, nullptr);
	ASSERT_NE(item, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(quadMaterial, stage));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 quad = CenterTexel(pixels, size);

	stage.quad->SetActive(false);
	Handle<TestSprite> sprite = AddSprite(stage.scene, item, Vec3(0.0f, 0.0f, 0.5f), Color(0.2f, 0.6f, 0.9f, 1.0f));
	sprite->halfSize = 0.25f;
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	ASSERT_EQ(sprite->recorded, 1u);
	const Vec3 shaded = CenterTexel(pixels, size);
	EXPECT_GT(shaded.y, 60.0f);
	EXPECT_NEAR(shaded.x, quad.x, 3.0f);
	EXPECT_NEAR(shaded.y, quad.y, 3.0f);
	EXPECT_NEAR(shaded.z, quad.z, 3.0f);

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, CastingItemShadowsEverywhere) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	WriteProjectFile("Tally.hlsl", SHADOW_TALLY_SHADER);
	Asset* tallyShader = AssetDatabase::LoadAsset(Path("project://Tally.hlsl"));
	ASSERT_NE(tallyShader, nullptr);
	MaterialAsset* ground = LoadMaterial("Tally.mat", tallyShader->assetId, "");
	MaterialAsset* caster = LoadShaderMaterial("Caster.mat", "Caster.hlsl", CASTER_SPRITE_SHADER, "");
	MaterialAsset* plain = LoadShaderMaterial("Plain.mat", "Plain.hlsl", SPRITE_SHADER, "");
	ASSERT_NE(ground, nullptr);
	ASSERT_NE(caster, nullptr);
	ASSERT_NE(plain, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(ground, stage));
	stage.quad->SetLocalScale(Vec3(8.0f, 8.0f, 1.0f));
	SceneSystem::EndFrame();
	Handle<TestSprite> sprite = AddSprite(stage.scene, caster, Vec3(0.0f, 0.0f, 1.0f), Color::White());
	sprite->halfSize = 0.2f;

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	const auto shadowed = [&] {
		EXPECT_TRUE(RenderStage(stage, pixels, size));
		u32 count = 0;
		for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
			count += pixels[i + 2] > 100 && pixels[i + 2] < 180 && pixels[i] < 128 ? 1u : 0u;
		}
		return count;
	};

	EXPECT_EQ(shadowed(), 0u);
	EXPECT_EQ(sprite->shadowRecorded, 0u);

	sprite->castShadows = true;
	EXPECT_GT(shadowed(), 30u);
	EXPECT_GT(sprite->shadowRecorded, 0u);
	const Mat4 cameraView = QuadView().view;
	bool same = true;
	for (u32 row = 0; row < 4; ++row) {
		for (u32 column = 0; column < 4; ++column) {
			same = same && std::abs(sprite->shadowView(row, column) - cameraView(row, column)) < 1.0e-4f;
		}
	}
	EXPECT_FALSE(same);

	sprite->GetOwner()->SetLocalPosition(Vec3(2.44f, 0.0f, 2.5f));
	SceneSystem::EndFrame();
	const u32 drawnBefore = sprite->recorded;
	EXPECT_GT(shadowed(), 10u);
	EXPECT_EQ(sprite->recorded, drawnBefore);

	sprite->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, 1.0f));
	sprite->material = Handle<MaterialAsset>(plain);
	SceneSystem::EndFrame();
	const u32 castBefore = sprite->shadowRecorded;
	EXPECT_EQ(shadowed(), 0u);
	EXPECT_EQ(sprite->shadowRecorded, castBefore);

	TearDownStage(stage);
}

TEST_F(ProceduralRendererFixture, OpaqueItemInPrepassOnly) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShaderAsset* kernel = LoadKernelFixture("ComputeDepthFixture.hlsl");
	ASSERT_NE(kernel, nullptr);
	MaterialAsset* wall = LoadUnlit("Wall.mat", Color(0.0f, 0.0f, 0.0f, 1.0f), false);
	MaterialAsset* caster = LoadShaderMaterial("Caster.mat", "Caster.hlsl", CASTER_SPRITE_SHADER, "");
	MaterialAsset* reader = LoadShaderMaterial("Depth.mat", "Depth.hlsl", DEPTH_READING_SHADER, "");
	ASSERT_NE(wall, nullptr);
	ASSERT_NE(caster, nullptr);
	ASSERT_NE(reader, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(wall, stage));
	stage.quad->SetLocalScale(Vec3(8.0f, 8.0f, 1.0f));
	SceneSystem::EndFrame();
	Handle<TestSprite> opaque = AddSprite(stage.scene, caster, Vec3(0.0f, 0.0f, 2.0f), Color::Black());
	opaque->halfSize = 0.3f;
	Handle<TestSprite> reading = AddSprite(stage.scene, reader, Vec3(0.0f, 0.0f, 2.5f), Color::White());
	reading->halfSize = 0.05f;

	const ViewDesc quadView = QuadView();
	const auto depthAt = [&](f32 z) {
		const Vec4 clip = quadView.projection * (quadView.view * Vec4(0.0f, 0.0f, z, 1.0f));
		return clip.z / clip.w;
	};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_GT(opaque->prepassRecorded, 0u);
	EXPECT_GT(CenterTexel(pixels, size).x, 245.0f);

	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", stage.scene);
	Handle<Camera> camera = cameraObject->AddComponent<Camera>();
	SceneSystem::EndFrame();
	RHI::Device& device = *RenderSystem::GetDevice();
	RHI::BufferDesc valuesDesc;
	valuesDesc.size = 16;
	valuesDesc.usage = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferSrc;
	auto keep = std::make_unique<DepthKeepPass>();
	keep->kernel = Handle<ShaderAsset>(kernel);
	keep->camera = camera;
	keep->values = device.CreateBufferResource(valuesDesc);
	ASSERT_TRUE(FramePasses::Register(*keep, &ARGUMENTS_PASS_OWNER));
	ViewDesc keeping = QuadView();
	keeping.camera = camera;
	for (u32 frame = 0; frame < 2; ++frame) {
		RenderSystem::SubmitView(stage.target, keeping);
		RenderSystem::RenderAll();
		ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));
		EXPECT_GT(CenterTexel(pixels, size).x, 245.0f);
	}
	ASSERT_TRUE(keep->answer.image.IsValid());
	device.WaitIdle();
	f32 kept = -1.0f;
	device.ReadbackBuffer(keep->values, {reinterpret_cast<u8*>(&kept), sizeof(kept)});
	EXPECT_NEAR(kept, depthAt(0.0f), 1.0e-4f);
	EXPECT_GT(std::abs(kept - depthAt(2.0f)), 1.0e-3f);
	FramePasses::UnregisterOwnedBy(&ARGUMENTS_PASS_OWNER);

	opaque->GetOwner()->SetActive(false);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_LT(CenterTexel(pixels, size).x, 235.0f);

	TearDownStage(stage);
	device.DestroyResource(keep->values);
}
