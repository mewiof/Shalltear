#include "TestsPCH.h"

#include "Graphics/GraphicsSettings.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Graphics/ObjectPass.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include "PassProbes.h"
#include "ZoneProbe.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

	constexpr const char* CHANNEL_ROTATE_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneColor;\n"
		"[[vk::binding(1, 2)]] SamplerState sltSceneSampler;\n"
		"struct VSOutput { float4 positionCS : SV_Position; float2 uv : TEXCOORD0; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"\toutput.uv = float2(corner.x, 1.0 - corner.y);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\tfloat4 scene = sltSceneColor.Sample(sltSceneSampler, input.uv);\n"
		"\treturn float4(scene.g, scene.b, scene.r, 1) * tint;\n"
		"}\n";

	constexpr const char* FLAT_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"struct VSOutput { float4 positionCS : SV_Position; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target { return tint; }\n";

	f32 EncodedByte(f32 linear) {
		linear = Math::Clamp(linear, 0.0f, 1.0f);
		const f32 encoded = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
		return encoded * 255.0f;
	}

	constexpr const char* NORMALS_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneNormals;\n"
		"[[vk::binding(1, 2)]] SamplerState sltSceneSampler;\n"
		"struct VSOutput { float4 positionCS : SV_Position; float2 uv : TEXCOORD0; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"\toutput.uv = float2(corner.x, 1.0 - corner.y);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\treturn float4(sltSceneNormals.Sample(sltSceneSampler, input.uv).rgb, 1);\n"
		"}\n";

	constexpr const char* DEPTH_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneDepth;\n"
		"[[vk::binding(1, 2)]] SamplerState sltSceneSampler;\n"
		"struct VSOutput { float4 positionCS : SV_Position; float2 uv : TEXCOORD0; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"\toutput.uv = float2(corner.x, 1.0 - corner.y);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\treturn float4(sltSceneDepth.Sample(sltSceneSampler, input.uv).rrr, 1);\n"
		"}\n";

	class FullScreenPassFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mounted = false;
		bool _projectMounted = false;
		Handle<ShaderAsset> _gridShader;
		ViewTargetId _target = INVALID_VIEW_TARGET;
		Vec2I _size{128, 128};
		Handle<Scene> _scene;

		void InitializeAt(i32 samples) {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_fspass_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");

			SettingsPaths paths;
			paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			Settings::Initialize(paths);

			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			FileSystem::Mount("project", Path(_root.generic_string()));
			_projectMounted = true;
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			if (!RenderSystem::IsRenderingAvailable()) {
				return;
			}

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

			_target = RenderSystem::CreateViewTarget(_size);
			if (Asset* asset = AssetDatabase::LoadAsset(Path("engine://Shaders/EditorGrid.hlsl"))) {
				_gridShader = Handle<ShaderAsset>(static_cast<ShaderAsset*>(asset));
			}
		}

		void TearDown() override {
			if (_scene.IsValid()) {
				SceneSystem::UnloadScene(_scene);
				SceneSystem::EndFrame();
			}
			if (_target != INVALID_VIEW_TARGET) {
				RenderSystem::DestroyViewTarget(_target);
			}
			_gridShader = {};
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			if (_projectMounted) {
				FileSystem::Unmount("project");
			}
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool Ready() const {
			return RenderSystem::IsRenderingAvailable() && _target != INVALID_VIEW_TARGET && _gridShader.IsValid();
		}

		void WriteProjectFile(const char* name, const str& content) {
			FileSystem::WriteTextFile(Path((_root / name).generic_string()), content);
		}

		bool CompilerAvailable() {
			WriteProjectFile("CompilerProbe.hlsl",
							 "struct VSOutput { float4 positionCS : SV_Position; };\n"
							 "VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
							 "\tVSOutput output; output.positionCS = float4(float(vertexId), 0, 0, 1); return output;\n"
							 "}\n"
							 "float4 PSMain(VSOutput input) : SV_Target { return float4(1, 0, 1, 1); }\n");
			ShaderAsset probe;
			return probe.Import(FileSystem::Resolve(Path("project://CompilerProbe.hlsl")));
		}

		MaterialAsset* LoadMaterial(const char* fileName, const char* shaderFile, const str& jsonBody) {
			Asset* shader = AssetDatabase::LoadAsset(Path(str("project://") + shaderFile));
			if (!shader) {
				return nullptr;
			}
			str document = "{\n\t\"shader\": \"" + shader->assetId.ToString() + "\"";
			if (!jsonBody.empty()) {
				document += ",\n" + jsonBody;
			}
			document += "\n}\n";
			WriteProjectFile(fileName, document);
			return static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
		}

		bool Render(std::span<const Handle<RenderPass>> rows, Vector<u8>& outPixels, Vec2I& outSize) {
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
			return RenderSystem::ReadbackViewTarget(_target, outPixels, outSize);
		}

		void BuildCamera(Mat4& view, Mat4& projection, Vec3& position) const {
			position = Vec3(0.0f, 3.0f, 6.0f);
			view = Mat4::LookAt(position, Vec3(0.0f, 0.0f, 0.0f));
			projection = Mat4::PerspectiveReverseZ(60.0f, static_cast<f32>(_size.x) / static_cast<f32>(_size.y),
												   Ops::EDITOR_NEAR_PLANE, Ops::EDITOR_FAR_PLANE);
		}

		bool AddOpaqueQuad(f32 yawDeg) {
			Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			if (!unlitShader || !quadMesh) {
				return false;
			}
			WriteProjectFile("Surface.mat", "{\n\t\"shader\": \"" + unlitShader->assetId.ToString() + "\"\n}\n");
			auto* surface = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Surface.mat")));
			if (!surface) {
				return false;
			}
			_scene = SceneSystem::CreateScene("Prepass");
			Handle<SceneObject> object = SceneSystem::CreateObject("Quad", _scene);
			object->SetLocalRotation(Quat::AngleAxis(yawDeg, Vec3::Up()));
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(surface));
			SceneSystem::EndFrame();
			return true;
		}

		PassProbes::GridPush MakeGridPush() const {
			Mat4 view, projection;
			Vec3 position;
			BuildCamera(view, projection, position);
			return PassProbes::MakeVisiblePush(view, projection, position);
		}

		static Color32 TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
			return Color32(pixels[index], pixels[index + 1], pixels[index + 2], pixels[index + 3]);
		}

		static u32 TexelsNotMatchingTheRotationOf(const Vector<u8>& reference, const Vector<u8>& rotated, Vec2I size) {
			constexpr i32 TOLERANCE = 2;
			u32 mismatched = 0;
			for (i32 y = 0; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					const Color32 source = TexelAt(reference, size, x, y);
					const Color32 result = TexelAt(rotated, size, x, y);
					if (std::abs(static_cast<i32>(result.r) - static_cast<i32>(source.g)) > TOLERANCE ||
						std::abs(static_cast<i32>(result.g) - static_cast<i32>(source.b)) > TOLERANCE ||
						std::abs(static_cast<i32>(result.b) - static_cast<i32>(source.r)) > TOLERANCE) {
						++mismatched;
					}
				}
			}
			return mismatched;
		}

		static u32 NonBlackTexels(const Vector<u8>& pixels, Vec2I size) {
			u32 count = 0;
			for (i32 y = 0; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					Color32 texel = TexelAt(pixels, size, x, y);
					if (texel.r != 0 || texel.g != 0 || texel.b != 0) {
						++count;
					}
				}
			}
			return count;
		}
	};

} // anonymous namespace

TEST(FullScreenPassRequests, RowRequestsWhatMaterialsShaderDeclares) {
	FullScreenPass row;
	EXPECT_EQ(row.GetRequests(), PassRequests::None);

	ShaderAsset shader;
	MaterialAsset material;
	material.shader = Handle<ShaderAsset>(&shader);
	row.material = Handle<MaterialAsset>(&material);
	EXPECT_EQ(row.GetRequests(), PassRequests::None);
	auto declare = [&shader](u32 set, const char* name) {
		ShaderResourceInfo resource;
		resource.set = set;
		resource.binding = static_cast<u32>(shader.resources.size());
		resource.type = RHI::DescriptorType::SampledTexture;
		resource.name = name;
		shader.resources.push_back(resource);
	};
	declare(2, "sltSceneColor");
	EXPECT_EQ(row.GetRequests(), PassRequests::SceneColor);
	declare(2, "sltSceneNormals");
	EXPECT_EQ(row.GetRequests(), PassRequests::SceneColor | PassRequests::SceneNormals);
	declare(1, "sltSceneDepth");
	declare(2, "sltPassInput0");
	EXPECT_EQ(row.GetRequests(), PassRequests::SceneColor | PassRequests::SceneNormals);

	ObjectPass plain;
	EXPECT_EQ(plain.GetRequests(), PassRequests::None);
}

TEST(FullScreenPassRequests, RowDrawsViewlessShader) {
	ShaderAsset shader;
	ShaderResourceInfo scene;
	scene.set = 2;
	scene.binding = 4;
	scene.type = RHI::DescriptorType::SampledTexture;
	scene.name = "sltSceneColor";
	shader.resources.push_back(scene);
	EXPECT_TRUE(FullScreenPass::Draws(shader));

	shader.pushConstantSize = 8;
	EXPECT_FALSE(FullScreenPass::Draws(shader));
	shader.pushConstantSize = 0;

	ShaderResourceInfo view;
	view.set = 0;
	view.binding = 0;
	view.type = RHI::DescriptorType::UniformBuffer;
	view.name = "ShalltearView";
	shader.resources.insert(shader.resources.begin(), view);
	EXPECT_FALSE(FullScreenPass::Draws(shader));

	ShaderAsset kernel;
	ShaderVariant compute;
	compute.computeSpirv.assign(16, 0xAB);
	kernel.variants.push_back(std::move(compute));
	EXPECT_FALSE(FullScreenPass::Draws(kernel));
}

TEST_F(FullScreenPassFixture, PostColorReadsResolvedScene) {
	InitializeAt(4);
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	if (RenderSystem::GetSampleCount() != 4) {
		GTEST_SKIP() << "adapter doesn't offer 4x on the scene chain";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Rotate.hlsl", CHANNEL_ROTATE_SHADER);
	MaterialAsset* material = LoadMaterial("Rotate.mat", "Rotate.hlsl",
										   "\t\"colors\": { \"tint\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	const PassProbes::GridPush push = MakeGridPush();
	PassProbes::DrawingProbePass grid;
	grid.shader = _gridShader;
	grid.bytes = {reinterpret_cast<const u8*>(&push), PassProbes::GRID_PUSH_BYTES};
	grid.point = PassPoint::AfterOpaque;

	FullScreenPass row;
	row.point = PassPoint::Post;
	row.material = Handle<MaterialAsset>(material);

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&grid), Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_TRUE(grid.drew);

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	const Handle<RenderPass> gridOnly[] = {Handle<RenderPass>(&grid)};
	ASSERT_TRUE(Render(gridOnly, reference, referenceSize));
	ASSERT_GT(NonBlackTexels(reference, referenceSize), 200u);
	ASSERT_EQ(size, referenceSize);

	ASSERT_GT(NonBlackTexels(pixels, size), 200u);

	EXPECT_EQ(TexelsNotMatchingTheRotationOf(reference, pixels, size), 0u);
}

TEST_F(FullScreenPassFixture, SingleSampleCopyIsBlit) {
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Rotate.hlsl", CHANNEL_ROTATE_SHADER);
	MaterialAsset* material = LoadMaterial("Rotate.mat", "Rotate.hlsl",
										   "\t\"colors\": { \"tint\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	const PassProbes::GridPush push = MakeGridPush();
	PassProbes::DrawingProbePass grid;
	grid.shader = _gridShader;
	grid.bytes = {reinterpret_cast<const u8*>(&push), PassProbes::GRID_PUSH_BYTES};
	grid.point = PassPoint::AfterOpaque;

	FullScreenPass row;
	row.point = PassPoint::Post;
	row.material = Handle<MaterialAsset>(material);

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&grid), Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_TRUE(grid.drew);

	Vector<u8> reference{GetMemoryResource(MemTag::Graphics)};
	Vec2I referenceSize{};
	const Handle<RenderPass> gridOnly[] = {Handle<RenderPass>(&grid)};
	ASSERT_TRUE(Render(gridOnly, reference, referenceSize));
	ASSERT_GT(NonBlackTexels(reference, referenceSize), 200u);
	ASSERT_EQ(size, referenceSize);
	ASSERT_GT(NonBlackTexels(pixels, size), 200u);
	EXPECT_EQ(TexelsNotMatchingTheRotationOf(reference, pixels, size), 0u);
}

namespace {

	class UnrequestingPass : public RenderPass {
	public:
		Handle<MaterialAsset> material;

		void Record(PassContext& context) override { context.DrawFullScreen(material); }
	};

} // anonymous namespace

TEST_F(FullScreenPassFixture, UnaskedColorReadsBlack) {
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device or no grid shader";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Rotate.hlsl", CHANNEL_ROTATE_SHADER);
	MaterialAsset* material = LoadMaterial("Rotate.mat", "Rotate.hlsl",
										   "\t\"colors\": { \"tint\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	const PassProbes::GridPush push = MakeGridPush();
	PassProbes::DrawingProbePass grid;
	grid.shader = _gridShader;
	grid.bytes = {reinterpret_cast<const u8*>(&push), PassProbes::GRID_PUSH_BYTES};
	grid.point = PassPoint::AfterOpaque;

	UnrequestingPass row;
	row.point = PassPoint::Post;
	row.material = Handle<MaterialAsset>(material);

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&grid), Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	ASSERT_TRUE(grid.drew);
	EXPECT_EQ(NonBlackTexels(pixels, size), 0u);
}

TEST_F(FullScreenPassFixture, ResourcelessMaterialDraws) {
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Flat.hlsl", FLAT_SHADER);
	MaterialAsset* material = LoadMaterial("Flat.mat", "Flat.hlsl",
										   "\t\"colors\": { \"tint\": [0, 1, 0, 1] }");
	ASSERT_NE(material, nullptr);

	FullScreenPass row;
	row.point = PassPoint::AfterOpaque;
	row.material = Handle<MaterialAsset>(material);
	EXPECT_EQ(row.GetRequests(), PassRequests::None);

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	const Color32 center = TexelAt(pixels, size, size.x / 2, size.y / 2);
	EXPECT_LT(center.r, 40);
	EXPECT_GT(center.g, 200);
}

TEST_F(FullScreenPassFixture, PassDrawReadsViewsRecordByName) {
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Record.hlsl", "#pragma shalltear_blend opaque\n"
									"#pragma shalltear_depth off\n"
									"#pragma shalltear_cull off\n"
									"#include \"ViewData.hlsli\"\n"
									"[[vk::binding(0, 2)]] cbuffer sltViewRecord { ShalltearViewData sltPassView; };\n"
									"struct VSOutput { float4 positionCS : SV_Position; };\n"
									"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
									"\tVSOutput output;\n"
									"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
									"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
									"\treturn output;\n"
									"}\n"
									"float4 PSMain(VSOutput input) : SV_Target {\n"
									"\tfloat4x4 view = ShalltearViewDataMatrix(sltPassView.view[0], sltPassView.view[1], sltPassView.view[2], sltPassView.view[3]);\n"
									"\tfloat4x4 inverseView = ShalltearViewDataMatrix(sltPassView.inverseView[0], sltPassView.inverseView[1],\n"
									"\t\tsltPassView.inverseView[2], sltPassView.inverseView[3]);\n"
									"\tfloat4x4 product = mul(inverseView, view);\n"
									"\tfloat error = 0.0;\n"
									"\t[unroll] for (uint row = 0; row < 4; ++row) {\n"
									"\t\terror += dot(abs(product[row] - float4(row == 0, row == 1, row == 2, row == 3)), 1.0);\n"
									"\t}\n"
									"\treturn float4(sltPassView.screenParams.x / 256.0, sltPassView.inverseView[3].y / 4.0, saturate(1.0 - error * 50.0), 1.0);\n"
									"}\n");
	MaterialAsset* material = LoadMaterial("Record.mat", "Record.hlsl", "");
	ASSERT_NE(material, nullptr);

	FullScreenPass row;
	row.point = PassPoint::AfterOpaque;
	row.material = Handle<MaterialAsset>(material);
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));

	const Color32 center = TexelAt(pixels, size, size.x / 2, size.y / 2);
	EXPECT_NEAR(static_cast<f32>(center.r), EncodedByte(static_cast<f32>(_size.x) / 256.0f), 2.0f);
	EXPECT_NEAR(static_cast<f32>(center.g), EncodedByte(3.0f / 4.0f), 2.0f);
	EXPECT_NEAR(static_cast<f32>(center.b), EncodedByte(1.0f), 2.0f);
}

TEST_F(FullScreenPassFixture, ARowWithNoMaterialDrawsNothing) {
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	FullScreenPass row;
	row.point = PassPoint::AfterOpaque;

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	EXPECT_EQ(NonBlackTexels(pixels, size), 0u);
}

TEST_F(FullScreenPassFixture, TemplatedMaterialRefused) {
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	Asset* lit = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(lit, nullptr);
	str document = "{\n\t\"shader\": \"" + lit->assetId.ToString() + "\"\n}\n";
	WriteProjectFile("Lit.mat", document);
	auto* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://Lit.mat")));
	ASSERT_NE(material, nullptr);

	FullScreenPass row;
	row.point = PassPoint::AfterOpaque;
	row.material = Handle<MaterialAsset>(material);

	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	EXPECT_EQ(NonBlackTexels(pixels, size), 0u);
}

TEST_F(FullScreenPassFixture, NormalsToggleDepthPrepass) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Normals.hlsl", NORMALS_SHADER);
	MaterialAsset* material = LoadMaterial("Normals.mat", "Normals.hlsl", "");
	ASSERT_NE(material, nullptr);
	WriteProjectFile("Flat.hlsl", FLAT_SHADER);
	MaterialAsset* flat = LoadMaterial("Flat.mat", "Flat.hlsl", "\t\"colors\": { \"tint\": [0, 0, 0, 1] }");
	ASSERT_NE(flat, nullptr);

	FullScreenPass row;
	row.point = PassPoint::AfterOpaque;
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};

	row.material = Handle<MaterialAsset>(flat);
	ASSERT_TRUE(Render(rows, pixels, size));
	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(rows, pixels, size));
		Profiler::NextFrame();
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Prepass"), 0u);
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Scene"), 1u);
	}

	row.material = Handle<MaterialAsset>(material);
	ASSERT_TRUE(Render(rows, pixels, size));
	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(rows, pixels, size));
		Profiler::NextFrame();
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Prepass"), 1u);
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Scene"), 1u);
	}

	row.material = Handle<MaterialAsset>(flat);
	ASSERT_TRUE(Render(rows, pixels, size));
	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(rows, pixels, size));
		Profiler::NextFrame();
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Prepass"), 0u);
	}
#endif
}

TEST_F(FullScreenPassFixture, PrepassDrawsRealGeometryIntoNormalsAttachment) {
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	constexpr f32 YAW_DEG = 40.0f;
	ASSERT_TRUE(AddOpaqueQuad(YAW_DEG));
	WriteProjectFile("Normals.hlsl", NORMALS_SHADER);
	MaterialAsset* material = LoadMaterial("Normals.mat", "Normals.hlsl", "");
	ASSERT_NE(material, nullptr);

	FullScreenPass row;
	row.point = PassPoint::AfterOpaque;
	row.material = Handle<MaterialAsset>(material);
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));

	const f32 yaw = YAW_DEG * Math::DEG_TO_RAD;
	const Color32 center = TexelAt(pixels, size, size.x / 2, size.y / 2);
	EXPECT_NEAR(static_cast<f32>(center.r), EncodedByte(std::sin(yaw) * 0.5f + 0.5f), 3.0f);
	EXPECT_NEAR(static_cast<f32>(center.g), EncodedByte(0.5f), 3.0f);
	EXPECT_NEAR(static_cast<f32>(center.b), EncodedByte(std::cos(yaw) * 0.5f + 0.5f), 3.0f);

	const Color32 corner = TexelAt(pixels, size, 1, 1);
	EXPECT_NEAR(static_cast<f32>(corner.b), EncodedByte(1.0f), 3.0f);
	EXPECT_GT(std::abs(static_cast<i32>(center.r) - static_cast<i32>(corner.r)), 8);
}

TEST_F(FullScreenPassFixture, DepthOnlyPrepassHasNoNormals) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	InitializeAt(1);
	if (!Ready()) {
		GTEST_SKIP() << "no device";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Depth.hlsl", DEPTH_SHADER);
	MaterialAsset* material = LoadMaterial("Depth.mat", "Depth.hlsl", "");
	ASSERT_NE(material, nullptr);

	FullScreenPass row;
	row.point = PassPoint::AfterOpaque;
	row.material = Handle<MaterialAsset>(material);
	const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rows, pixels, size));
	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(rows, pixels, size));
		Profiler::NextFrame();
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Prepass"), 1u);
	}
	const Color32 center = TexelAt(pixels, size, size.x / 2, size.y / 2);
	EXPECT_LT(center.r, 8);
#endif
}
