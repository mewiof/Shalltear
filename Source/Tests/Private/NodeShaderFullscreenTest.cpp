#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "GraphSurface.h"
#include "NodeShaderImporter.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <cmath>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;

namespace {

	using namespace ShadingTest;

	struct FullscreenBuilder : GraphBuilder {
		GraphContext* fragment = nullptr;

		FullscreenBuilder() {
			Graph("target", "\"fullscreen\"");
			fragment = &Context(1, "Context/Fragment");
			Block(*fragment, 2, "Block/BaseColor");
			Block(*fragment, 3, "Block/Alpha");
		}
	};

	struct Generated {
		bool ok = false;
		str text;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};

		bool Says(std::string_view fragment) const { return text.find(fragment) != str::npos; }
		bool Names(u32 node, std::string_view fragment) const {
			return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const GraphDiagnostic& diagnostic) {
				return diagnostic.node == node && diagnostic.message.find(fragment) != str::npos;
			});
		}
		str First() const { return diagnostics.empty() ? str() : diagnostics[0].message; }
	};

	Generated GenerateFullscreen(const GraphDocument& document) {
		Generated out;
		out.ok = GraphSurface::Generate(document, "Test.slshader", out.text, out.diagnostics);
		return out;
	}

	void Paint(GraphBuilder& b, u32 node, std::string_view port) {
		b.Node(90, "Math/Saturate");
		b.Connect(node, port, 90, "In");
		b.Connect(90, "Out", 2, "Value");
	}

} // anonymous namespace

TEST(NodeShaderFullscreenTest, FullscreenGraphIsRawShader) {
	FullscreenBuilder b;
	b.Node(20, "Input/Color");
	DocumentBuilder::Setting(b.document.nodes.back(), "Color", "[0.2, 0.4, 0.6, 1]");
	b.Connect(20, "Out", 2, "Value");
	const Generated generated = GenerateFullscreen(b.document);
	ASSERT_TRUE(generated.ok) << generated.First();
	EXPECT_FALSE(generated.Says("shalltear_template")) << generated.text;
	EXPECT_TRUE(generated.Says("VSMain(uint vertexId : SV_VertexID)"));
	EXPECT_TRUE(generated.Says("PSMain(ShalltearFullscreenVaryings input) : SV_Target"));
	EXPECT_TRUE(generated.Says("#pragma shalltear_depth off"));
	EXPECT_TRUE(generated.Says("#pragma shalltear_cull off"));
	EXPECT_FALSE(generated.Says("sltViewRecord"));
	EXPECT_FALSE(generated.Says("sltScene"));

	EXPECT_TRUE(GraphSurface::IsBlockActive(b.document, "Block/BaseColor"));
	EXPECT_TRUE(GraphSurface::IsBlockActive(b.document, "Block/Alpha"));
	EXPECT_FALSE(GraphSurface::IsBlockActive(b.document, "Block/Metallic"));
	EXPECT_FALSE(GraphSurface::IsBlockActive(b.document, "Block/Position"));
	EXPECT_TRUE(GraphSurface::IsFullscreen(b.document));
	EXPECT_TRUE(GraphSurface::IsMaterialShaderSource(GraphDocumentOps::Write(b.document)));
}

TEST(NodeShaderFullscreenTest, FullscreenGraphDeclaresWhatValuesRead) {
	FullscreenBuilder clock;
	clock.Node(20, "Input/Time");
	Paint(clock, 20, "Time");
	const Generated timed = GenerateFullscreen(clock.document);
	ASSERT_TRUE(timed.ok) << timed.First();
	EXPECT_TRUE(timed.Says("cbuffer sltViewRecord"));
	EXPECT_TRUE(timed.Says("#include \"ViewData.hlsli\""));
	EXPECT_FALSE(timed.Says("sltSceneDepth"));

	FullscreenBuilder located;
	located.Node(20, "Input/Position");
	Paint(located, 20, "Out");
	const Generated position = GenerateFullscreen(located.document);
	ASSERT_TRUE(position.ok) << position.First();
	EXPECT_TRUE(position.Says("Texture2D sltSceneDepth"));
	EXPECT_TRUE(position.Says("SamplerState sltSceneSampler"));
	EXPECT_TRUE(position.Says("cbuffer sltViewRecord"));
	EXPECT_FALSE(position.Says("sltSceneNormals"));

	FullscreenBuilder facing;
	facing.Node(20, "Input/NormalVector");
	Paint(facing, 20, "Out");
	const Generated normal = GenerateFullscreen(facing.document);
	ASSERT_TRUE(normal.ok) << normal.First();
	EXPECT_TRUE(normal.Says("Texture2D sltSceneNormals"));
	EXPECT_FALSE(normal.Says("sltSceneDepth"));
	EXPECT_FALSE(normal.Says("sltViewRecord"));
}

TEST(NodeShaderFullscreenTest, FullscreenRefusesMissingInputs) {
	FullscreenBuilder vertex;
	vertex.Node(20, "Input/VertexID");
	Paint(vertex, 20, "Out");
	const Generated id = GenerateFullscreen(vertex.document);
	EXPECT_FALSE(id.ok);
	EXPECT_TRUE(id.Names(20, "has no vertex stage")) << id.First();

	FullscreenBuilder tangent;
	tangent.Node(20, "Input/TangentVector");
	Paint(tangent, 20, "Out");
	const Generated frame = GenerateFullscreen(tangent.document);
	EXPECT_FALSE(frame.ok);
	EXPECT_TRUE(frame.Names(20, "no tangent frame")) << frame.First();
}

TEST(NodeShaderFullscreenTest, EitherTargetSaysWhenItReadsClock) {
	FullscreenBuilder screen;
	screen.Node(20, "Input/Time");
	Paint(screen, 20, "Time");
	const Generated timed = GenerateFullscreen(screen.document);
	ASSERT_TRUE(timed.ok) << timed.First();
	EXPECT_TRUE(GraphSurface::ReadsClock(timed.text));

	FullscreenBuilder still;
	still.Node(20, "Input/UV");
	Paint(still, 20, "Out");
	const Generated untimed = GenerateFullscreen(still.document);
	ASSERT_TRUE(untimed.ok) << untimed.First();
	EXPECT_FALSE(GraphSurface::ReadsClock(untimed.text));

	UnlitBuilder surface;
	surface.Node(20, "Input/Time");
	surface.Node(90, "Math/Saturate");
	surface.Connect(20, "Time", 90, "In");
	surface.Connect(90, "Out", 2, "Value");
	Generated surfaced;
	surfaced.ok = GraphSurface::Generate(surface.document, "Test.slshader", surfaced.text, surfaced.diagnostics);
	ASSERT_TRUE(surfaced.ok) << surfaced.First();
	EXPECT_TRUE(GraphSurface::ReadsClock(surfaced.text));
}

TEST(NodeShaderFullscreenTest, ScenesImagesAreFullScreenDraws) {
	struct Reader {
		std::string_view def;
		std::string_view port;
		std::string_view key;
		std::string_view value;
		std::string_view image;
	};
	const Reader READERS[] = {
		{"Input/SceneColor", "Out", "", "", "Texture2D sltSceneColor"},
		{"Input/SceneDepth", "Out", "Sampling Mode", "\"Raw\"", "Texture2D sltSceneDepth"},
		{"Input/SampleBuffer", "Output", "Source Buffer", "\"NormalWorldSpace\"", "Texture2D sltSceneNormals"},
		{"Input/SampleBuffer", "Output", "Source Buffer", "\"BlitSource\"", "Texture2D sltSceneColor"},
	};
	const std::string_view IMAGES[] = {"Texture2D sltSceneColor", "Texture2D sltSceneDepth", "Texture2D sltSceneNormals"};
	auto read = [](GraphBuilder& b, const Reader& reader) {
		GraphNode& node = b.Node(20, reader.def);
		if (!reader.key.empty()) {
			DocumentBuilder::Setting(node, reader.key, reader.value);
		}
		Paint(b, 20, reader.port);
	};
	for (const Reader& reader : READERS) {
		FullscreenBuilder screen;
		read(screen, reader);
		const Generated drawn = GenerateFullscreen(screen.document);
		ASSERT_TRUE(drawn.ok) << reader.def << " " << reader.value << ": " << drawn.First();
		for (std::string_view image : IMAGES) {
			EXPECT_EQ(drawn.Says(image), image == reader.image) << reader.def << " " << reader.value << " and " << image;
		}
		EXPECT_TRUE(drawn.Says("SamplerState sltSceneSampler")) << reader.def;

		UnlitBuilder surface;
		read(surface, reader);
		Generated refused;
		refused.ok = GraphSurface::Generate(surface.document, "Test.slshader", refused.text, refused.diagnostics);
		EXPECT_FALSE(refused.ok) << reader.def << " " << reader.value;
		EXPECT_TRUE(refused.Names(20, "no image of the scene")) << reader.def << ": " << refused.First();
	}

	FullscreenBuilder motion;
	read(motion, {"Input/SampleBuffer", "Output", "Source Buffer", "\"MotionVectors\"", ""});
	const Generated vectors = GenerateFullscreen(motion.document);
	EXPECT_FALSE(vectors.ok);
	EXPECT_TRUE(vectors.Names(20, "motion vectors")) << vectors.First();

	FullscreenBuilder eye;
	read(eye, {"Input/SceneDepth", "Out", "Sampling Mode", "\"Eye\"", ""});
	const Generated distance = GenerateFullscreen(eye.document);
	ASSERT_TRUE(distance.ok) << distance.First();
	EXPECT_TRUE(distance.Says("cbuffer sltViewRecord"));
	FullscreenBuilder raw;
	read(raw, {"Input/SceneDepth", "Out", "Sampling Mode", "\"Raw\"", ""});
	EXPECT_FALSE(GenerateFullscreen(raw.document).Says("cbuffer sltViewRecord"));
}

TEST(NodeShaderFullscreenTest, SampleBufferIsAsWideAsBuffer) {
	const std::pair<std::string_view, GraphValueType> BUFFERS[] = {{"\"NormalWorldSpace\"", GraphValueType::Float3},
																   {"\"BlitSource\"", GraphValueType::Float4}};
	for (const auto& [buffer, type] : BUFFERS) {
		DocumentBuilder b;
		DocumentBuilder::Setting(b.Node(20, "Input/SampleBuffer"), "Source Buffer", buffer);
		const Compiled compiled = Compile(b.document, {{20, "Output"}}, GraphStageFragment, GraphSurface::FullscreenBindings());
		ASSERT_TRUE(compiled.ok) << buffer << ": " << compiled.Report();
		EXPECT_EQ(compiled.program.At(compiled.program.roots[0].value).type, type) << buffer;
	}
}

TEST(NodeShaderFullscreenTest, FullscreenTargetAnswersEveryNameSurfaceDoes) {
	const auto table = GraphSurface::FullscreenBindings();
	for (const GraphBinding& binding : GraphSurface::Bindings()) {
		const auto found = std::find_if(table.begin(), table.end(), [&](const GraphBinding& candidate) { return candidate.name == binding.name; });
		ASSERT_NE(found, table.end()) << binding.name;
		EXPECT_EQ(found->type, binding.type) << binding.name;
	}
}

namespace {

	class NodeShaderFullscreenFixture : public MaterialReadbackFixture {
	protected:
		bool Available() {
			if (!RenderSystem::IsRenderingAvailable()) {
				_why = "no Vulkan device available";
				return false;
			}
			if (!CompilerAvailable()) {
				_why = "dxcompiler.dll unavailable";
				return false;
			}
			return true;
		}

		MaterialAsset* AddGraphMaterial(const char* graphFile, const GraphDocument& document, const char* materialFile) {
			WriteProjectFile(graphFile, GraphDocumentOps::Write(document));
			Asset* shader = AssetDatabase::LoadAsset(Path(str("project://") + graphFile));
			return shader ? LoadMaterial(materialFile, shader->assetId, {}) : nullptr;
		}

		bool BuildSurfaceStage(ShadingStage& stage) {
			Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			return unlit && BuildStage(LoadMaterial("Surface.mat", unlit->assetId, {}), stage);
		}

		bool RenderWithRow(const ShadingStage& stage, MaterialAsset* material, Vector<u8>& pixels, Vec2I& size) {
			FullScreenPass row;
			row.material = Handle<MaterialAsset>(material);
			const Handle<RenderPass> rows[] = {Handle<RenderPass>(&row)};
			ViewDesc desc = QuadView();
			desc.passes = rows;
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
		}

		static Vec3 TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
			return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
		}

		str _why;
	};

	Vec3 QuadPlanePointAt(Vec2I size, i32 x, i32 y) {
		const f32 reach = Eye.z * std::tan(Math::DegToRad(30.0f));
		const f32 ndcX = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(size.x) * 2.0f - 1.0f;
		const f32 ndcY = 1.0f - (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size.y) * 2.0f;
		return Vec3(-ndcX * reach, ndcY * reach, 0.0f);
	}

} // anonymous namespace

TEST_F(NodeShaderFullscreenFixture, TheFullscreenTemplateIsTheConventionalOne) {
	Vector<AssetBrowserOps::CreateMenuEntry> entries{GetMemoryResource(MemTag::Editor)};
	AssetBrowserOps::GetCreateMenuEntries(entries);
	const auto entry = std::find_if(entries.begin(), entries.end(),
									[](const AssetBrowserOps::CreateMenuEntry& candidate) { return candidate.label == "Node Shader/Fullscreen"; });
	ASSERT_NE(entry, entries.end());
	GraphDocument document;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(NodeShaderImporter::TemplateDocument(entry->templateIndex), document, error)) << error;
	EXPECT_TRUE(GraphSurface::IsFullscreen(document));
	ASSERT_EQ(document.contexts.size(), 1u);
	ASSERT_EQ(document.contexts[0].blocks.size(), 2u);
	EXPECT_EQ(document.contexts[0].blocks[0].def, "Block/BaseColor");
	EXPECT_EQ(document.contexts[0].blocks[1].def, "Block/Alpha");
	const Generated generated = GenerateFullscreen(document);
	EXPECT_TRUE(generated.ok) << generated.First();
}

TEST_F(NodeShaderFullscreenFixture, FullscreenRowPaintsStack) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	FullscreenBuilder b;
	b.Node(20, "Input/UV");
	b.Node(21, "Channel/Split");
	b.Connect(20, "Out", 21, "In");
	b.Node(22, "Input/Screen");
	b.Node(23, "Math/Divide");
	b.Connect(22, "Width", 23, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[128]");
	b.Node(24, "Channel/Combine");
	b.Connect(21, "R", 24, "R");
	b.Connect(21, "G", 24, "G");
	b.Connect(23, "Out", 24, "B");
	b.Connect(24, "RGB", 2, "Value");
	MaterialAsset* material = AddGraphMaterial("Screen.slshader", b.document, "Screen.mat");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildSurfaceStage(stage));
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderWithRow(stage, material, pixels, size));

	for (const i32 at : {8, 56}) {
		const Vec3 texel = TexelAt(pixels, size, at, at);
		const f32 uv = (static_cast<f32>(at) + 0.5f) / static_cast<f32>(size.x);
		const Vec3 expected = ExpectedTexel(Vec3(uv, uv, static_cast<f32>(size.x) / 128.0f));
		EXPECT_NEAR(texel.x, expected.x, 2.0f) << "u at " << at;
		EXPECT_NEAR(texel.y, expected.y, 2.0f) << "v at " << at;
		EXPECT_NEAR(texel.z, expected.z, 2.0f);
	}
	TearDownStage(stage);
}

TEST_F(NodeShaderFullscreenFixture, FullscreenGraphReadsPositionUnderPixel) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	FullscreenBuilder b;
	b.Node(20, "Input/Position");
	b.Node(21, "Math/Add");
	b.Connect(20, "Out", 21, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5, 0.5, 0.5]");
	Paint(b, 21, "Out");
	MaterialAsset* material = AddGraphMaterial("Position.slshader", b.document, "Position.mat");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildSurfaceStage(stage));
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderWithRow(stage, material, pixels, size));

	const i32 cx = size.x / 2;
	const i32 cy = size.y / 2;
	for (const Vec2I at : {Vec2I{cx, cy}, Vec2I{cx, cy - 6}, Vec2I{cx, cy + 6}, Vec2I{cx - 6, cy}, Vec2I{cx + 6, cy}}) {
		const Vec3 point = QuadPlanePointAt(size, at.x, at.y);
		const Vec3 expected = ExpectedTexel(point + Vec3(0.5f, 0.5f, 0.5f));
		const Vec3 texel = TexelAt(pixels, size, at.x, at.y);
		EXPECT_NEAR(texel.x, expected.x, 3.0f) << "x at (" << at.x << ", " << at.y << ")";
		EXPECT_NEAR(texel.y, expected.y, 3.0f) << "y at (" << at.x << ", " << at.y;
		EXPECT_NEAR(texel.z, expected.z, 3.0f) << "z at (" << at.x << ", " << at.y;
	}
	TearDownStage(stage);
}

TEST_F(NodeShaderFullscreenFixture, AFullscreenGraphRowReadsSceneColor) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlit, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(LoadMaterial("Surface.mat", unlit->assetId, "\t\"colors\": { \"baseColor\": [0.8, 0.4, 0.1, 1] }"), stage));
	Vector<u8> scene{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, scene, size));
	const i32 cx = size.x / 2;
	const i32 cy = size.y / 2;
	const Vec3 quad = TexelAt(scene, size, cx, cy);
	ASSERT_GT(quad.x, quad.y + 20.0f);
	ASSERT_GT(quad.y, quad.z + 20.0f);

	struct Reader {
		const char* def;
		const char* port;
		const char* buffer;
		const char* graph;
		const char* material;
	};
	const Reader READERS[] = {{"Input/SceneColor", "Out", nullptr, "SceneColor.slshader", "SceneColor.mat"},
							  {"Input/SampleBuffer", "Output", "\"BlitSource\"", "BlitSource.slshader", "BlitSource.mat"}};
	for (const Reader& reader : READERS) {
		FullscreenBuilder b;
		GraphNode& read = b.Node(20, reader.def);
		if (reader.buffer) {
			DocumentBuilder::Setting(read, "Source Buffer", reader.buffer);
		}
		b.Node(21, "Channel/Split");
		b.Connect(20, reader.port, 21, "In");
		b.Node(22, "Channel/Combine");
		b.Connect(21, "G", 22, "R");
		b.Connect(21, "B", 22, "G");
		b.Connect(21, "R", 22, "B");
		b.Connect(22, "RGB", 2, "Value");
		MaterialAsset* material = AddGraphMaterial(reader.graph, b.document, reader.material);
		ASSERT_NE(material, nullptr) << reader.def;

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		ASSERT_TRUE(RenderWithRow(stage, material, pixels, size));
		const Vec3 turned = TexelAt(pixels, size, cx, cy);
		EXPECT_NEAR(turned.x, quad.y, 2.0f) << reader.def;
		EXPECT_NEAR(turned.y, quad.z, 2.0f) << reader.def;
		EXPECT_NEAR(turned.z, quad.x, 2.0f) << reader.def;
		const Vec3 corner = TexelAt(pixels, size, 2, 2);
		EXPECT_LT(corner.x + corner.y + corner.z, 3.0f) << reader.def;
	}
	TearDownStage(stage);
}

TEST_F(NodeShaderFullscreenFixture, SceneDepthModesRead) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	FullscreenBuilder b;
	DocumentBuilder::Setting(b.Node(20, "Input/SceneDepth"), "Sampling Mode", "\"Linear01\"");
	DocumentBuilder::Setting(b.Node(21, "Input/SceneDepth"), "Sampling Mode", "\"Raw\"");
	DocumentBuilder::Setting(b.Node(22, "Input/SceneDepth"), "Sampling Mode", "\"Eye\"");
	b.Node(23, "Math/Multiply");
	b.Connect(20, "Out", 23, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[10]");
	b.Node(24, "Math/Divide");
	b.Connect(22, "Out", 24, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[4]");
	b.Node(25, "Channel/Combine");
	b.Connect(23, "Out", 25, "R");
	b.Connect(21, "Out", 25, "G");
	b.Connect(24, "Out", 25, "B");
	b.Connect(25, "RGB", 2, "Value");
	MaterialAsset* material = AddGraphMaterial("Depth.slshader", b.document, "Depth.mat");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildSurfaceStage(stage));
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderWithRow(stage, material, pixels, size));

	constexpr f32 NEAR_PLANE = 0.1f;
	constexpr f32 FAR_PLANE = 100.0f;
	const f32 distance = Eye.z;
	const f32 raw = NEAR_PLANE * (FAR_PLANE - distance) / (distance * (FAR_PLANE - NEAR_PLANE));
	const Vec3 expected = ExpectedTexel(Vec3(distance / FAR_PLANE * 10.0f, raw, distance / 4.0f));
	const Vec3 texel = TexelAt(pixels, size, size.x / 2, size.y / 2);
	EXPECT_NEAR(texel.x, expected.x, 2.0f);
	EXPECT_NEAR(texel.y, expected.y, 2.0f);
	EXPECT_NEAR(texel.z, expected.z, 2.0f);
	const Vec3 past = TexelAt(pixels, size, 2, 2);
	EXPECT_GT(past.x, 253.0f);
	EXPECT_LT(past.y, 2.0f);
	EXPECT_GT(past.z, 253.0f);
	TearDownStage(stage);
}

TEST_F(NodeShaderFullscreenFixture, SampleBufferReadsTheScenesNormal) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	FullscreenBuilder b;
	b.Node(20, "Input/SampleBuffer");
	b.Node(21, "Math/Multiply");
	b.Connect(20, "Output", 21, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5, 0.5, 0.5]");
	b.Node(22, "Math/Add");
	b.Connect(21, "Out", 22, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5, 0.5, 0.5]");
	b.Connect(22, "Out", 2, "Value");
	MaterialAsset* material = AddGraphMaterial("Normals.slshader", b.document, "Normals.mat");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildSurfaceStage(stage));
	stage.quad->SetLocalRotation(Quat::AngleAxis(30.0f, Vec3::Up()));
	SceneSystem::EndFrame();
	const Vec3 normal = stage.quad->TransformDirection(Vec3(0.0f, 0.0f, 1.0f)).Normalized();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderWithRow(stage, material, pixels, size));

	const Vec3 expected = ExpectedTexel(normal * 0.5f + Vec3(0.5f, 0.5f, 0.5f));
	const Vec3 texel = TexelAt(pixels, size, size.x / 2, size.y / 2);
	EXPECT_NEAR(texel.x, expected.x, 3.0f);
	EXPECT_NEAR(texel.y, expected.y, 3.0f);
	EXPECT_NEAR(texel.z, expected.z, 3.0f);
	TearDownStage(stage);
}
