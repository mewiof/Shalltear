#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "GraphSurface.h"
#include "NodeShaderImporter.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/Environment.h"
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

	struct SkyBuilder : GraphBuilder {
		GraphContext* fragment = nullptr;

		SkyBuilder() {
			Graph("target", "\"sky\"");
			fragment = &Context(1, "Context/Fragment");
			Block(*fragment, 2, "Block/BaseColor");
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

	Generated GenerateGraph(const GraphDocument& document) {
		Generated out;
		out.ok = GraphSurface::Generate(document, "Test.slshader", out.text, out.diagnostics);
		return out;
	}

	void Paint(GraphBuilder& b, u32 node, std::string_view port) {
		b.Node(90, "Math/Saturate");
		b.Connect(node, port, 90, "In");
		b.Connect(90, "Out", 2, "Value");
	}

	void PaintDirection(GraphBuilder& b, u32 node, std::string_view port) {
		b.Node(90, "Math/Multiply");
		b.Connect(node, port, 90, "A");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5, 0.5, 0.5]");
		b.Node(91, "Math/Add");
		b.Connect(90, "Out", 91, "A");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5, 0.5, 0.5]");
		b.Connect(91, "Out", 2, "Value");
	}

	void BranchOnto(GraphBuilder& b, u32 branch, u32 node, std::string_view port) {
		b.Node(branch, "Utility/Branch");
		b.Connect(node, port, branch, "Predicate");
		DocumentBuilder::Constant(b.document.nodes.back(), "True", "[1]");
		DocumentBuilder::Constant(b.document.nodes.back(), "False", "[0]");
	}

	constexpr std::string_view SKY_NAMES[] = {"SkyDirection", "SkySunDirection", "SkySunColor", "SkySunAngularDiameter",
											  "SkySunEnabled", "SkyCapturing", "SkySunDisc"};

	const GraphBinding* FindIn(std::span<const GraphBinding> table, std::string_view name) {
		for (const GraphBinding& binding : table) {
			if (binding.name == name) {
				return &binding;
			}
		}
		return nullptr;
	}

} // anonymous namespace

TEST(NodeShaderSkyTest, SkyGraphWritesBaseColor) {
	SkyBuilder b;
	b.Node(20, "Input/Color");
	DocumentBuilder::Setting(b.document.nodes.back(), "Color", "[0.2, 0.4, 0.6, 1]");
	b.Connect(20, "Out", 2, "Value");
	b.Block(*b.fragment, 3, "Block/Alpha");
	b.Block(*b.fragment, 4, "Block/Emission");
	b.Node(21, "Input/Time");
	b.Connect(21, "Time", 3, "Value");
	b.Node(22, "Input/Sky");
	b.Connect(22, "Sun Color", 4, "Value");
	const Generated generated = GenerateGraph(b.document);
	ASSERT_TRUE(generated.ok) << generated.First();
	EXPECT_TRUE(generated.Says("#pragma shalltear_template sky\n"));
	EXPECT_TRUE(generated.Says("void ShalltearSkyFunction(ShalltearSkyInput input, inout ShalltearSkyOutput sky) {"));
	EXPECT_TRUE(generated.Says("\tsky.color = "));
	EXPECT_FALSE(generated.Says("shalltear_blend")) << generated.text;
	EXPECT_FALSE(generated.Says("shalltear_depth"));
	EXPECT_FALSE(generated.Says("shalltear_cull"));
	EXPECT_FALSE(generated.Says("input.time")) << generated.text;
	EXPECT_FALSE(generated.Says("input.sunColor"));

	EXPECT_TRUE(GraphSurface::IsBlockActive(b.document, "Block/BaseColor"));
	EXPECT_FALSE(GraphSurface::IsBlockActive(b.document, "Block/Alpha"));
	EXPECT_FALSE(GraphSurface::IsBlockActive(b.document, "Block/Emission"));
	EXPECT_FALSE(GraphSurface::IsBlockActive(b.document, "Block/Position"));
	EXPECT_FALSE(GraphSurface::IsBlockActive(b.document, GraphSurface::RADIANCE_BLOCK_ID));
	EXPECT_TRUE(GraphSurface::IsSky(b.document));
	EXPECT_FALSE(GraphSurface::IsFullscreen(b.document));
	EXPECT_FALSE(GraphSurface::HasVertexStage(b.document));
	EXPECT_TRUE(GraphSurface::IsMaterialShaderSource(GraphDocumentOps::Write(b.document)));

	b.Graph("supportEffects", "true");
	EXPECT_FALSE(GraphSurface::SupportsEffects(b.document));
	GraphSurfaceFunctions functions;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(GraphSurface::GenerateFunctions(b.document, functions, diagnostics));
	EXPECT_TRUE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& d) { return d.message.find("a Sky graph") != str::npos; }));
}

TEST(NodeShaderSkyTest, AbsentBaseColorLeavesContractsDefault) {
	SkyBuilder unwired;
	const Generated grey = GenerateGraph(unwired.document);
	ASSERT_TRUE(grey.ok) << grey.First();
	EXPECT_TRUE(grey.Says("\tsky.color = "));

	SkyBuilder absent;
	absent.fragment->blocks.clear();
	const Generated black = GenerateGraph(absent.document);
	ASSERT_TRUE(black.ok) << black.First();
	EXPECT_FALSE(black.Says("sky.color")) << black.text;
}

TEST(NodeShaderSkyTest, SkyTargetAnswersEveryNameOthersDo) {
	const auto sky = GraphSurface::SkyBindings();
	for (const std::span<const GraphBinding> other : {GraphSurface::Bindings(), GraphSurface::FullscreenBindings()}) {
		for (const GraphBinding& binding : other) {
			const GraphBinding* found = FindIn(sky, binding.name);
			ASSERT_NE(found, nullptr) << binding.name;
			EXPECT_EQ(found->type, binding.type) << binding.name;
		}
		for (const GraphBinding& binding : sky) {
			const GraphBinding* found = FindIn(other, binding.name);
			ASSERT_NE(found, nullptr);
			EXPECT_EQ(found->type, binding.type) << binding.name;
		}
		for (std::string_view name : SKY_NAMES) {
			const GraphBinding* refused = FindIn(other, name);
			ASSERT_NE(refused, nullptr) << name;
			EXPECT_NE(refused->unavailable.find("only a Sky graph"), str::npos) << name;
		}
	}
	for (std::string_view name : SKY_NAMES) {
		const GraphBinding* supplied = FindIn(sky, name);
		ASSERT_NE(supplied, nullptr) << name;
		EXPECT_TRUE(supplied->unavailable.empty()) << name;
	}
}

TEST(NodeShaderSkyTest, WhatSkyLacksIsRefusedNamingWhy) {
	const struct {
		std::string_view def;
		std::string_view port;
		std::string_view key;
		std::string_view value;
		std::string_view said;
	} READERS[] = {
		{"Input/Position", "Out", "Space", "\"World\"", "the Sky node's Direction is the ray"},
		{"Input/Position", "Out", "Space", "\"Object\"", "the Sky node's Direction is the ray"},
		{"Input/Position", "Out", "Space", "\"Tangent\"", "the Sky node's Direction is the ray"},
		{"Input/ViewVector", "Out", "", "", "the Sky node's Direction is the ray"},
		{"Input/NormalVector", "Out", "", "", "the Sky node's Direction is the ray"},
		{"Input/TangentVector", "Out", "", "", "the Sky node's Direction is the ray"},
		{"Input/UV", "Out", "", "", "the Sky node's Direction is the ray"},
		{"Input/VertexColor", "Out", "", "", "the Sky node's Direction is the ray"},
		{"Input/VertexID", "Out", "", "", "has no vertex stage"},
		{"Input/SceneColor", "Out", "", "", "no image of the scene"},
	};
	for (const auto& reader : READERS) {
		SCOPED_TRACE(str(reader.def) + " " + str(reader.value));
		SkyBuilder b;
		GraphNode& read = b.Node(20, reader.def);
		if (!reader.key.empty()) {
			DocumentBuilder::Setting(read, reader.key, reader.value);
		}
		Paint(b, 20, reader.port);
		const Generated generated = GenerateGraph(b.document);
		EXPECT_FALSE(generated.ok) << generated.text;
		EXPECT_TRUE(generated.Names(20, reader.said)) << generated.First();
	}
}

TEST(NodeShaderSkyTest, SkyNodesAreSkyGraphsAlone) {
	for (std::string_view target : {"\"unlit\"", "\"fullscreen\""}) {
		for (std::string_view def : {"Input/Sky", "Input/SunDisc"}) {
			SCOPED_TRACE(str(target) + " " + str(def));
			GraphBuilder b;
			b.Graph("target", target);
			b.Block(b.Context(1, "Context/Fragment"), 2, "Block/BaseColor");
			b.Node(20, def);
			Paint(b, 20, def == "Input/Sky" ? "Sun Direction" : "Out");
			const Generated generated = GenerateGraph(b.document);
			EXPECT_FALSE(generated.ok);
			EXPECT_TRUE(generated.Names(20, "only a Sky graph")) << generated.First();
		}
	}

	const struct {
		std::string_view port;
		std::string_view reads;
	} OUTPUTS[] = {{"Direction", "input.directionWS"},
				   {"Sun Direction", "input.sunDirectionWS"},
				   {"Sun Color", "input.sunColor"},
				   {"Sun Angular Diameter", "input.sunAngularDiameter"},
				   {"Sun Enabled", "input.sunEnabled"},
				   {"Capturing", "input.capturing"}};
	for (const auto& output : OUTPUTS) {
		SCOPED_TRACE(output.port);
		SkyBuilder b;
		b.Node(20, "Input/Sky");
		if (output.port == "Sun Enabled" || output.port == "Capturing") {
			BranchOnto(b, 21, 20, output.port);
			Paint(b, 21, "Out");
		} else {
			Paint(b, 20, output.port);
		}
		const Generated generated = GenerateGraph(b.document);
		ASSERT_TRUE(generated.ok) << generated.First();
		EXPECT_TRUE(generated.Says(output.reads)) << generated.text;
	}
	SkyBuilder disc;
	disc.Node(20, "Input/SunDisc");
	disc.Connect(20, "Out", 2, "Value");
	const Generated drawn = GenerateGraph(disc.document);
	ASSERT_TRUE(drawn.ok) << drawn.First();
	EXPECT_TRUE(drawn.Says("ShalltearSkySunDisc(input)")) << drawn.text;
}

TEST(NodeShaderSkyTest, SkyGraphSaysWhenItReadsClock) {
	SkyBuilder timed;
	timed.Node(20, "Input/Time");
	Paint(timed, 20, "Time");
	const Generated clock = GenerateGraph(timed.document);
	ASSERT_TRUE(clock.ok) << clock.First();
	EXPECT_TRUE(GraphSurface::ReadsClock(clock.text));

	SkyBuilder still;
	still.Node(20, "Input/Sky");
	Paint(still, 20, "Direction");
	const Generated ray = GenerateGraph(still.document);
	ASSERT_TRUE(ray.ok) << ray.First();
	EXPECT_FALSE(GraphSurface::ReadsClock(ray.text));
}

namespace {

	class NodeShaderSkyFixture : public MaterialReadbackFixture {
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

		bool Render(const ShadingStage& stage, const ViewDesc& desc, Vector<u8>& pixels, Vec2I& size) {
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

	constexpr f32 TEXEL_TOLERANCE = 1.5f;

	void ExpectTexelNear(const Vec3& actual, const Vec3& expected, const char* what) {
		EXPECT_NEAR(actual.x, expected.x, TEXEL_TOLERANCE) << what;
		EXPECT_NEAR(actual.y, expected.y, TEXEL_TOLERANCE) << what;
		EXPECT_NEAR(actual.z, expected.z, TEXEL_TOLERANCE) << what;
	}

	constexpr Vec2I PIXEL_A{48, 12};
	constexpr Vec2I PIXEL_B{9, 55};

} // anonymous namespace

TEST_F(NodeShaderSkyFixture, TheSkyTemplateIsTheCreateMenus) {
	Vector<AssetBrowserOps::CreateMenuEntry> entries{GetMemoryResource(MemTag::Editor)};
	AssetBrowserOps::GetCreateMenuEntries(entries);
	const auto entry = std::find_if(entries.begin(), entries.end(),
									[](const AssetBrowserOps::CreateMenuEntry& candidate) { return candidate.label == "Node Shader/Sky"; });
	ASSERT_NE(entry, entries.end());
	GraphDocument document;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(NodeShaderImporter::TemplateDocument(entry->templateIndex), document, error)) << error;
	EXPECT_TRUE(GraphSurface::IsSky(document));
	ASSERT_EQ(document.contexts.size(), 1u);
	ASSERT_EQ(document.contexts[0].blocks.size(), 1u);
	EXPECT_EQ(document.contexts[0].blocks[0].def, "Block/BaseColor");
	const Generated generated = GenerateGraph(document);
	EXPECT_TRUE(generated.ok) << generated.First();
}

TEST_F(NodeShaderSkyFixture, SkyGraphIsFunctionAlongRay) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	SkyBuilder ray;
	ray.Node(20, "Input/Sky");
	PaintDirection(ray, 20, "Direction");
	MaterialAsset* sky = AddGraphMaterial("Ray.slshader", ray.document, "Ray.mat");
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(sky->shader.Get(), nullptr);
	ASSERT_EQ(sky->shader->integrator, ShaderTemplate::Sky);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));

	ViewDesc desc = QuadView();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, desc, pixels, size));
	for (const Vec2I pixel : {PIXEL_A, PIXEL_B}) {
		const Vec3 direction = SkyPixelRay(desc, size, pixel.x, pixel.y);
		ExpectTexelNear(TexelAt(pixels, size, pixel.x, pixel.y), ExpectedTexel(direction * 0.5f + Vec3(0.5f, 0.5f, 0.5f)),
						"the sky graph is not its function along the pixel's ray");
	}
	EXPECT_GT(std::fabs(TexelAt(pixels, size, PIXEL_A.x, PIXEL_A.y).y - TexelAt(pixels, size, PIXEL_B.x, PIXEL_B.y).y), 20.0f);

	SkyBuilder toward;
	toward.Node(20, "Input/ViewDirection");
	PaintDirection(toward, 20, "Out");
	MaterialAsset* view = AddGraphMaterial("Toward.slshader", toward.document, "Toward.mat");
	ASSERT_NE(view, nullptr);
	environment->SetSkyMaterial(Handle<MaterialAsset>(view));
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, desc, pixels, size));
	for (const Vec2I pixel : {PIXEL_A, PIXEL_B}) {
		const Vec3 direction = SkyPixelRay(desc, size, pixel.x, pixel.y);
		ExpectTexelNear(TexelAt(pixels, size, pixel.x, pixel.y), ExpectedTexel(direction * -0.5f + Vec3(0.5f, 0.5f, 0.5f)),
						"View Direction is not the ray turned toward the camera");
	}

	TearDownStage(stage);
}

TEST_F(NodeShaderSkyFixture, SunReachesSkyGraph) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	SkyBuilder towardSun;
	towardSun.Node(20, "Input/Sky");
	PaintDirection(towardSun, 20, "Sun Direction");
	SkyBuilder colored;
	colored.Node(20, "Input/Sky");
	colored.Node(21, "Math/Multiply");
	colored.Connect(20, "Sun Color", 21, "A");
	DocumentBuilder::Constant(colored.document.nodes.back(), "B", "[0.25, 0.25, 0.25]");
	colored.Connect(21, "Out", 2, "Value");
	SkyBuilder flags;
	flags.Node(20, "Input/Sky");
	BranchOnto(flags, 21, 20, "Sun Enabled");
	BranchOnto(flags, 22, 20, "Capturing");
	flags.Node(23, "Channel/Combine");
	flags.Connect(20, "Sun Angular Diameter", 23, "R");
	flags.Connect(21, "Out", 23, "G");
	flags.Connect(22, "Out", 23, "B");
	flags.Connect(23, "RGB", 2, "Value");
	MaterialAsset* direction = AddGraphMaterial("SunDirection.slshader", towardSun.document, "SunDirection.mat");
	MaterialAsset* color = AddGraphMaterial("SunColor.slshader", colored.document, "SunColor.mat");
	MaterialAsset* flagged = AddGraphMaterial("SunFlags.slshader", flags.document, "SunFlags.mat");
	ASSERT_NE(direction, nullptr);
	ASSERT_NE(color, nullptr);
	ASSERT_NE(flagged, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(direction, stage, environment));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(stage.lightDirection * 0.5f + Vec3(0.5f, 0.5f, 0.5f)),
					"the Sky node's Sun Direction does not point toward the light");

	stage.sun->SetColor(Color(1.0f, 0.5f, 0.25f, 1.0f));
	stage.sun->SetIntensity(2.0f);
	environment->SetSkyMaterial(Handle<MaterialAsset>(color));
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(0.5f, 0.25f, 0.125f)), "the Sun Color is not the light's color times its intensity");

	environment->SetSunDiscSizeDeg(20.0f);
	environment->SetSkyMaterial(Handle<MaterialAsset>(flagged));
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(Math::DegToRad(20.0f), 1.0f, 0.0f)),
					"the diameter, the sun's presence or the capture flag is not the view's");

	stage.sun->SetEnabled(false);
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	EXPECT_NEAR(CenterTexel(pixels, size).y, 0.0f, TEXEL_TOLERANCE);

	TearDownStage(stage);
}

TEST_F(NodeShaderSkyFixture, SkyGraphsSunDiscIsTemplatesOwn) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	WriteProjectFile("HandDisc.hlsl", "#pragma shalltear_template sky\n"
									  "void ShalltearSkyFunction(ShalltearSkyInput input, inout ShalltearSkyOutput sky) {\n"
									  "\tsky.color = ShalltearSkySunDisc(input);\n"
									  "}\n");
	Asset* handShader = AssetDatabase::LoadAsset(Path("project://HandDisc.hlsl"));
	ASSERT_NE(handShader, nullptr);
	MaterialAsset* hand = LoadMaterial("HandDisc.mat", handShader->assetId, {});
	SkyBuilder b;
	b.Node(20, "Input/SunDisc");
	b.Connect(20, "Out", 2, "Value");
	MaterialAsset* graph = AddGraphMaterial("Disc.slshader", b.document, "Disc.mat");
	ASSERT_NE(hand, nullptr);
	ASSERT_NE(graph, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(hand, stage, environment));
	environment->SetSunDiscSizeDeg(20.0f);
	SceneSystem::EndFrame();

	ViewDesc desc = QuadView();
	desc.cameraPosition = Vec3(0.0f, 0.0f, 0.0f);
	desc.view = Mat4::LookAt(desc.cameraPosition, stage.lightDirection);
	Vector<u8> written{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> drawn{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, desc, written, size));
	environment->SetSkyMaterial(Handle<MaterialAsset>(graph));
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, desc, drawn, size));
	ASSERT_EQ(written.size(), drawn.size());

	u32 lit = 0;
	u32 differing = 0;
	for (size_t i = 0; i < written.size(); i += 4) {
		lit += written[i + 1] > 128 ? 1u : 0u;
		for (size_t c = 0; c < 3; ++c) {
			differing += std::abs(static_cast<i32>(written[i + c]) - static_cast<i32>(drawn[i + c])) > 1 ? 1u : 0u;
		}
	}
	const u32 texels = static_cast<u32>(written.size() / 4);
	EXPECT_GT(lit, 50u);
	EXPECT_LT(lit, texels - 50u);
	EXPECT_EQ(differing, 0u);

	TearDownStage(stage);
}

TEST_F(NodeShaderSkyFixture, SkyGraphKnowsWhenCapturing) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	SkyBuilder b;
	b.Node(20, "Input/Sky");
	BranchOnto(b, 21, 20, "Capturing");
	b.Node(22, "Utility/Branch");
	b.Connect(20, "Capturing", 22, "Predicate");
	DocumentBuilder::Constant(b.document.nodes.back(), "True", "[0]");
	DocumentBuilder::Constant(b.document.nodes.back(), "False", "[1]");
	b.Node(23, "Channel/Combine");
	b.Connect(22, "Out", 23, "R");
	b.Connect(21, "Out", 23, "B");
	b.Connect(23, "RGB", 2, "Value");
	MaterialAsset* sky = AddGraphMaterial("Capturing.slshader", b.document, "Capturing.mat");
	Asset* lit = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(lit, nullptr);
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", lit->assetId, MIRROR_BODY);
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(TexelAt(pixels, size, 2, 2), ExpectedTexel(Vec3(1.0f, 0.0f, 0.0f)), "the visible sky is not the one drawn outside the capture");
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(MirrorOf(Vec3(0.0f, 0.0f, 1.0f))), "the reflection is not the capture's");

	TearDownStage(stage);
}

TEST_F(NodeShaderSkyFixture, SkyGraphReadsView) {
	if (!Available()) {
		GTEST_SKIP() << _why;
	}
	SkyBuilder b;
	b.Node(20, "Input/ScreenPosition");
	b.Node(21, "Channel/Split");
	b.Connect(20, "Out", 21, "In");
	b.Node(22, "Input/Time");
	b.Node(23, "Math/Multiply");
	b.Connect(22, "Time", 23, "A");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.1]");
	b.Node(24, "Math/Fraction");
	b.Connect(23, "Out", 24, "In");
	b.Node(25, "Channel/Combine");
	b.Connect(21, "R", 25, "R");
	b.Connect(21, "G", 25, "G");
	b.Connect(24, "Out", 25, "B");
	b.Connect(25, "RGB", 2, "Value");
	SkyBuilder camera;
	camera.Node(20, "Input/Camera");
	camera.Node(21, "Math/Multiply");
	camera.Connect(20, "Position", 21, "A");
	DocumentBuilder::Constant(camera.document.nodes.back(), "B", "[0.1, 0.1, 0.1]");
	camera.Node(22, "Math/Add");
	camera.Connect(21, "Out", 22, "A");
	DocumentBuilder::Constant(camera.document.nodes.back(), "B", "[0.5, 0.5, 0.5]");
	camera.Connect(22, "Out", 2, "Value");
	MaterialAsset* screen = AddGraphMaterial("Screen.slshader", b.document, "Screen.mat");
	MaterialAsset* eye = AddGraphMaterial("Eye.slshader", camera.document, "Eye.mat");
	ASSERT_NE(screen, nullptr);
	ASSERT_NE(eye, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(screen, stage, environment));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	for (const f64 time : {2.0, 7.5}) {
		ViewDesc desc = QuadView();
		desc.time = &time;
		ASSERT_TRUE(Render(stage, desc, pixels, size));
		for (const Vec2I pixel : {PIXEL_A, PIXEL_B}) {
			const f32 u = (static_cast<f32>(pixel.x) + 0.5f) / static_cast<f32>(size.x);
			const f32 v = (static_cast<f32>(pixel.y) + 0.5f) / static_cast<f32>(size.y);
			const f32 clock = static_cast<f32>(time * 0.1 - std::floor(time * 0.1));
			ExpectTexelNear(TexelAt(pixels, size, pixel.x, pixel.y), ExpectedTexel(Vec3(u, v, clock)),
							"the screen position or the view's clock did not reach the sky graph");
		}
	}

	environment->SetSkyMaterial(Handle<MaterialAsset>(eye));
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Eye * 0.1f + Vec3(0.5f, 0.5f, 0.5f)), "the Camera node is not the view's eye");

	TearDownStage(stage);
}
