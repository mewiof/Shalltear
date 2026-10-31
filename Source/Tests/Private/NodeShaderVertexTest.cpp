#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"
#include "ShadingHarness.h"

#include "GraphPreview.h"
#include "GraphSurface.h"
#include "NodeShaderEditOps.h"
#include "NodeShaderStackOps.h"

#include <set>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;
using namespace ShadingTest;

namespace Ops = NodeShaderEditOps;

namespace Stack = NodeShaderStackOps;

namespace {

	struct Generated {
		bool ok = false;
		str text;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};

		bool Has(std::string_view fragment) const { return text.find(fragment) != str::npos; }
		bool Names(u32 node, std::string_view fragment) const {
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				if (diagnostic.node == node && diagnostic.message.find(fragment) != str::npos) {
					return true;
				}
			}
			return false;
		}
		str Report() const {
			str out;
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				out += "node " + std::to_string(diagnostic.node) + ": " + diagnostic.message + "\n";
			}
			return out + text;
		}
	};

	Generated Generate(const GraphDocument& document) {
		Generated out;
		out.ok = GraphSurface::Generate(document, "project://Assets/Test.slshader", out.text, out.diagnostics);
		return out;
	}

	GraphBuilder UnlitStacks() {
		GraphBuilder b;
		b.Graph("target", "\"unlit\"");
		GraphContext& vertex = b.Context(1, "Context/Vertex");
		b.Block(vertex, 2, "Block/Position");
		b.Block(vertex, 3, "Block/Normal");
		b.Block(vertex, 4, "Block/Tangent");
		GraphContext& fragment = b.Context(10, "Context/Fragment");
		b.Block(fragment, 11, "Block/BaseColor");
		b.Block(fragment, 12, "Block/Alpha");
		return b;
	}

	GraphContext& VertexOf(GraphBuilder& b) { return b.document.contexts[0]; }

	GraphNode& Interpolator(GraphBuilder& b, u32 id, std::string_view name, std::string_view type = "Vector4",
							std::string_view mode = "Linear") {
		GraphNode& block = b.Block(VertexOf(b), id, "Block/CustomInterpolator");
		DocumentBuilder::Setting(block, "Name", Ops::Quote(name));
		DocumentBuilder::Setting(block, "Type", Ops::Quote(type));
		DocumentBuilder::Setting(block, "Interpolation", Ops::Quote(mode));
		return block;
	}

	GraphNode& Reader(GraphBuilder& b, u32 id, std::string_view name, std::string_view type = "Vector4") {
		GraphNode& node = b.Node(id, Stack::INTERPOLATOR_NODE);
		DocumentBuilder::Setting(node, "Interpolator", Ops::Quote(name));
		DocumentBuilder::Setting(node, "Type", Ops::Quote(type));
		return node;
	}

	GraphNode& Geometry(GraphBuilder& b, u32 id, std::string_view def, std::string_view space) {
		GraphNode& node = b.Node(id, def);
		DocumentBuilder::Setting(node, "Space", Ops::Quote(space));
		return node;
	}

} // anonymous namespace

TEST(NodeShaderVertexTest, UnwiredVertexStackWritesNoVertexFunction) {
	GraphBuilder b = UnlitStacks();
	Generated generated = Generate(b.document);
	ASSERT_TRUE(generated.ok) << generated.Report();
	EXPECT_FALSE(generated.Has("SHALLTEAR_VERTEX_FUNCTION")) << generated.text;
	EXPECT_FALSE(generated.Has("ShalltearVertexFunction(")) << generated.text;
}

TEST(NodeShaderVertexTest, WiredPositionBlockWritesVertexFunction) {
	GraphBuilder b = UnlitStacks();
	Geometry(b, 20, "Input/Position", "Object");
	b.Node(21, "Math/Add");
	DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0,1,0]");
	b.Connect(20, "Out", 21, "A");
	b.Connect(21, "Out", 2, "Value");
	Generated generated = Generate(b.document);
	ASSERT_TRUE(generated.ok) << generated.Report();
	EXPECT_TRUE(generated.Has("#define SHALLTEAR_VERTEX_FUNCTION\n")) << generated.text;
	EXPECT_TRUE(generated.Has("void ShalltearVertexFunction(ShalltearVertexInput input, inout ShalltearVertexOutput vertex) {")) << generated.text;
	EXPECT_TRUE(generated.Has("\tvertex.positionOS = ")) << generated.text;
	EXPECT_FALSE(generated.Has("vertex.normalOS"));
	EXPECT_FALSE(generated.Has("SHALLTEAR_INTERPOLATORS_"));
}

TEST(NodeShaderVertexTest, InterpolatorsPackFirstFitWithinTheirMode) {
	GraphBuilder b = UnlitStacks();
	b.Node(30, "Input/Vector4");
	const struct {
		u32 id;
		std::string_view name;
		std::string_view type;
		std::string_view mode;
	} blocks[] = {
		{40, "A", "Float", "Linear"},
		{41, "B", "Vector3", "Linear"},
		{42, "C", "Vector2", "NoPerspective"},
		{43, "D", "Float", "Linear"},
		{44, "E", "Vector4", "NoInterpolation"}};
	for (const auto& block : blocks) {
		Interpolator(b, block.id, block.name, block.type, block.mode);
		b.Connect(30, "Out", block.id, "Value");
	}
	Reader(b, 50, "B", "Vector3");
	b.Connect(50, "Out", 11, "Value");

	Generated generated = Generate(b.document);
	ASSERT_TRUE(generated.ok) << generated.Report();
	EXPECT_TRUE(generated.Has("#define SHALLTEAR_INTERPOLATORS_LINEAR 2\n")) << generated.text;
	EXPECT_TRUE(generated.Has("#define SHALLTEAR_INTERPOLATORS_NO_PERSPECTIVE 1\n")) << generated.text;
	EXPECT_TRUE(generated.Has("#define SHALLTEAR_INTERPOLATORS_FLAT 1\n")) << generated.text;
	EXPECT_TRUE(generated.Has("\tvertex.interpolators[0].x = ")) << generated.text;
	EXPECT_TRUE(generated.Has("\tvertex.interpolators[0].yzw = ")) << generated.text;
	EXPECT_TRUE(generated.Has("\tvertex.interpolators[1].x = ")) << generated.text;
	EXPECT_TRUE(generated.Has("\tvertex.interpolators[2].xy = ")) << generated.text;
	EXPECT_TRUE(generated.Has("\tvertex.interpolators[3].xyzw = ")) << generated.text;
	EXPECT_TRUE(generated.Has("input.interpolators[0].yzw")) << generated.text;
}

TEST(NodeShaderVertexTest, ExcessInterpolatorRefused) {
	GraphBuilder b = UnlitStacks();
	b.Node(30, "Input/Vector4");
	for (u32 i = 0; i < 17; ++i) {
		Interpolator(b, 40 + i, "I" + std::to_string(i));
		b.Connect(30, "Out", 40 + i, "Value");
	}
	Generated generated = Generate(b.document);
	EXPECT_FALSE(generated.ok);
	EXPECT_TRUE(generated.Names(56, "past the 16")) << generated.Report();
	EXPECT_FALSE(generated.Names(55, "past the 16"));
}

TEST(NodeShaderVertexTest, UnwiredInterpolatorIsFree) {
	GraphBuilder b = UnlitStacks();
	GraphNode& block = Interpolator(b, 40, "Tint", "Vector3");
	DocumentBuilder::Constant(block, "Value", "[0.25,0.5,0.75,1]");
	Reader(b, 50, "Tint", "Vector3");
	b.Connect(50, "Out", 11, "Value");
	Generated generated = Generate(b.document);
	ASSERT_TRUE(generated.ok) << generated.Report();
	EXPECT_FALSE(generated.Has("SHALLTEAR_INTERPOLATORS_")) << generated.text;
	EXPECT_FALSE(generated.Has("SHALLTEAR_VERTEX_FUNCTION")) << generated.text;
	EXPECT_TRUE(generated.Has("float3(0.25, 0.5, 0.75)")) << generated.text;
}

TEST(NodeShaderVertexTest, VertexStackRefusesUnsupported) {
	{
		GraphBuilder b = UnlitStacks();
		Reader(b, 50, "Missing");
		b.Connect(50, "Out", 11, "Value");
		Generated generated = Generate(b.document);
		EXPECT_FALSE(generated.ok);
		EXPECT_TRUE(generated.Names(50, "'Missing' (no block on the stack has that name)")) << generated.Report();
	}
	{
		GraphBuilder b = UnlitStacks();
		Interpolator(b, 40, "Twice");
		Interpolator(b, 41, "Twice");
		Generated generated = Generate(b.document);
		EXPECT_FALSE(generated.ok);
		EXPECT_TRUE(generated.Names(41, "2nd custom interpolator named 'Twice'")) << generated.Report();
	}
	{
		GraphBuilder b = UnlitStacks();
		b.Node(30, "Input/Vector4");
		Interpolator(b, 40, "Wind");
		b.Connect(30, "Out", 40, "Value");
		Reader(b, 50, "Wind", "Vector3");
		b.Connect(50, "Out", 2, "Value");
		Generated generated = Generate(b.document);
		EXPECT_FALSE(generated.ok);
		bool named = false;
		for (const GraphDiagnostic& diagnostic : generated.diagnostics) {
			named = named || diagnostic.node == 50;
		}
		EXPECT_TRUE(named) << generated.Report();
	}
	{
		GraphBuilder b = UnlitStacks();
		Geometry(b, 20, "Input/Position", "Object");
		b.Node(21, "Math/DDX");
		b.Connect(20, "Out", 21, "In");
		b.Connect(21, "Out", 2, "Value");
		Generated generated = Generate(b.document);
		EXPECT_FALSE(generated.ok);
		bool named = false;
		for (const GraphDiagnostic& diagnostic : generated.diagnostics) {
			named = named || diagnostic.node == 21;
		}
		EXPECT_TRUE(named) << generated.Report();
	}
}

TEST(NodeShaderVertexTest, PredisplacementReadSpendsSlotOnRestPosition) {
	GraphBuilder b = UnlitStacks();
	GraphNode& position = Geometry(b, 20, "Input/Position", "World");
	DocumentBuilder::Setting(position, "Source", Ops::Quote("Predisplacement"));
	b.Connect(20, "Out", 11, "Value");
	Generated generated = Generate(b.document);
	ASSERT_TRUE(generated.ok) << generated.Report();
	EXPECT_TRUE(generated.Has("#define SHALLTEAR_INTERPOLATORS_LINEAR 1\n")) << generated.text;
	EXPECT_TRUE(generated.Has("\tvertex.interpolators[0].xyz = input.positionWS;\n")) << generated.text;
	EXPECT_TRUE(generated.Has("float3 ShalltearGraphPredisplacementWS(ShalltearSurfaceInput input)")) << generated.text;
}

TEST(NodeShaderVertexTest, ACustomInterpolatorPreviewsItsBlocksInput) {
	GraphBuilder b = UnlitStacks();
	Geometry(b, 20, "Input/Position", "World");
	Interpolator(b, 40, "World", "Vector3");
	b.Connect(20, "Out", 40, "Value");
	Reader(b, 50, "World", "Vector3");
	b.Connect(50, "Out", 11, "Value");

	GraphPreview::Program program;
	ASSERT_TRUE(GraphPreview::Generate(b.document, "Test", program));
	bool tiled = false;
	for (const GraphPreview::Tile& tile : program.tiles) {
		tiled = tiled || tile.node == 50;
	}
	EXPECT_TRUE(tiled);
	EXPECT_FALSE(program.text.find("interpolators") != str::npos);
	for (const GraphDiagnostic& diagnostic : program.diagnostics) {
		ADD_FAILURE() << "node " << diagnostic.node << ": " << diagnostic.message;
	}
}

TEST(NodeShaderVertexTest, EnsureVertexStackAddsTheStackOnce) {
	GraphBuilder b;
	GraphContext& fragment = b.Context(1, "Context/Fragment");
	fragment.node.x = 300;
	fragment.node.y = 40;
	b.Block(fragment, 2, "Block/BaseColor");

	EXPECT_TRUE(Stack::EnsureVertexStack(b.document));
	ASSERT_EQ(b.document.contexts.size(), 2u);
	const GraphContext& vertex = b.document.contexts[0];
	EXPECT_EQ(vertex.node.def, "Context/Vertex");
	EXPECT_EQ(vertex.node.x, 300);
	EXPECT_LT(vertex.node.y, 40);
	ASSERT_EQ(vertex.blocks.size(), 3u);
	EXPECT_EQ(vertex.blocks[0].def, "Block/Position");
	EXPECT_EQ(vertex.blocks[1].def, "Block/Normal");
	EXPECT_EQ(vertex.blocks[2].def, "Block/Tangent");
	EXPECT_FALSE(Stack::EnsureVertexStack(b.document));
	EXPECT_EQ(b.document.contexts.size(), 2u);
}

TEST(NodeShaderVertexTest, VertexStackOrderAndNames) {
	GraphBuilder b = UnlitStacks();
	ASSERT_TRUE(Stack::RemoveBlock(b.document, 3));
	Vector<std::string_view> missing{GetMemoryResource(MemTag::Editor)};
	Stack::MissingBlocks(b.document, 1, missing);
	ASSERT_EQ(missing.size(), 2u);
	EXPECT_EQ(missing[0], "Block/Normal");
	EXPECT_EQ(missing[1], GraphSurface::INTERPOLATOR_BLOCK);

	const u32 normal = Stack::AddBlock(b.document, 1, "Block/Normal");
	ASSERT_NE(normal, 0u);
	ASSERT_EQ(VertexOf(b).blocks.size(), 3u);
	EXPECT_EQ(VertexOf(b).blocks[1].id, normal);

	const u32 first = Stack::AddBlock(b.document, 1, GraphSurface::INTERPOLATOR_BLOCK);
	const u32 second = Stack::AddBlock(b.document, 1, GraphSurface::INTERPOLATOR_BLOCK);
	ASSERT_NE(first, 0u);
	ASSERT_NE(second, 0u);
	EXPECT_EQ(VertexOf(b).blocks.back().id, second);
	Vector<u32> interpolators{GetMemoryResource(MemTag::Editor)};
	Stack::Interpolators(b.document, interpolators);
	ASSERT_EQ(interpolators.size(), 2u);
	const u32 reader = Stack::AddInterpolatorNode(b.document, first, {0.0f, 0.0f});
	const u32 other = Stack::AddInterpolatorNode(b.document, second, {0.0f, 0.0f});
	EXPECT_EQ(Stack::InterpolatorOf(b.document, reader), first);
	EXPECT_EQ(Stack::InterpolatorOf(b.document, other), second);
	EXPECT_EQ(Stack::AddBlock(b.document, 10, GraphSurface::INTERPOLATOR_BLOCK), 0u);
}

TEST(NodeShaderVertexTest, InterpolatorsReadersFollowRenameAndWidth) {
	GraphBuilder b = UnlitStacks();
	Interpolator(b, 40, "Wind");
	Interpolator(b, 41, "Sway");
	const u32 reader = Stack::AddInterpolatorNode(b.document, 40, {0.0f, 0.0f});
	ASSERT_EQ(Stack::InterpolatorOf(b.document, reader), 40u);

	EXPECT_FALSE(Stack::RenameInterpolator(b.document, 40, "Sway"));
	EXPECT_TRUE(Stack::RenameInterpolator(b.document, 40, "Gust"));
	EXPECT_EQ(Stack::InterpolatorOf(b.document, reader), 40u);

	EXPECT_TRUE(Stack::SetInterpolatorType(b.document, 40, "Vector2"));
	EXPECT_FALSE(Stack::SetInterpolatorType(b.document, 40, "Vector5"));
	const GraphNode* node = GraphDocumentOps::FindNode(b.document, reader);
	ASSERT_NE(node, nullptr);
	bool retyped = false;
	for (const GraphSetting& setting : node->settings) {
		retyped = retyped || (setting.key == "Type" && setting.value == "\"Vector2\"");
	}
	EXPECT_TRUE(retyped);
	EXPECT_TRUE(Stack::SetInterpolatorMode(b.document, 40, "NoInterpolation"));
	EXPECT_FALSE(Stack::SetInterpolatorMode(b.document, 40, "Flat"));
}

namespace {

	class NodeShaderVertexFixture : public MaterialReadbackFixture {
	protected:
		void SetUp() override {
			MaterialReadbackFixture::SetUp();
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			if (!CompilerAvailable()) {
				GTEST_SKIP() << "dxcompiler.dll unavailable";
			}
		}

		ShaderAsset* AddGraph(const char* fileName, const GraphDocument& document) {
			WriteProjectFile(fileName, GraphDocumentOps::Write(document));
			return static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path(str("project://") + fileName)));
		}

		static void Pose(ShadingStage& stage) {
			stage.quad->SetLocalRotation(Quat::FromEuler(Vec3(20.0f, 35.0f, 10.0f)));
			stage.quad->SetLocalScale(Vec3(1.3f, 0.8f, 1.0f));
			stage.sun->SetEnabled(false);
			SceneSystem::EndFrame();
		}

		bool Render(MaterialAsset* material, Vector<u8>& pixels, Vec2I& size, bool posed = true) {
			ShadingStage stage;
			if (!BuildStage(material, stage)) {
				return false;
			}
			if (posed) {
				Pose(stage);
			}
			const bool read = RenderStage(stage, pixels, size);
			TearDownStage(stage);
			return read;
		}

		static bool Covered(const Vector<u8>& pixels, size_t index) {
			return pixels[index] != 0 || pixels[index + 1] != 0 || pixels[index + 2] != 0;
		}
	};

	void Encode(GraphBuilder& b, u32 source, std::string_view port, u32 firstId) {
		b.Node(firstId, "Math/Multiply");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.25,0.25,0.25,0.25]");
		b.Node(firstId + 1, "Math/Add");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.5,0.5,0.5,0.5]");
		b.Node(firstId + 2, "Math/Saturate");
		b.Connect(source, port, firstId, "A");
		b.Connect(firstId, "Out", firstId + 1, "A");
		b.Connect(firstId + 1, "Out", firstId + 2, "In");
		b.Connect(firstId + 2, "Out", 11, "Value");
	}

	u32 MostOff(const Vector<u8>& a, const Vector<u8>& b, u32& covered) {
		u32 worst = 0;
		covered = 0;
		for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
			if (a[i] == 0 && a[i + 1] == 0 && a[i + 2] == 0) {
				continue;
			}
			++covered;
			for (size_t c = 0; c < 3; ++c) {
				worst = std::max<u32>(worst, static_cast<u32>(std::abs(static_cast<i32>(a[i + c]) - static_cast<i32>(b[i + c]))));
			}
		}
		return worst;
	}

} // anonymous namespace

TEST_F(NodeShaderVertexFixture, DisplacementMovesSurfaceAndShadow) {
	GraphBuilder b = UnlitStacks();
	b.document.settings.clear();
	b.Graph("target", "\"lit\"");
	b.Property(60, 20, "offset", "Vector3");
	Geometry(b, 21, "Input/Position", "Object");
	b.Node(22, "Math/Add");
	b.Connect(21, "Out", 22, "A");
	b.Connect(20, "Out", 22, "B");
	b.Connect(22, "Out", 2, "Value");
	ShaderAsset* shader = AddGraph("Displace.slshader", b.document);
	ASSERT_NE(shader, nullptr);
	MaterialAsset* displaced = LoadMaterial("Displaced.mat", shader->assetId, "\t\"vec3s\": { \"offset\": [2, 0, 0] }");
	MaterialAsset* resting = LoadMaterial("Resting.mat", shader->assetId, "\t\"vec3s\": { \"offset\": [0, 0, 0] }");
	ASSERT_NE(displaced, nullptr);
	ASSERT_NE(resting, nullptr);

	Asset* cubeAsset = AssetDatabase::LoadAsset(Path("engine://Meshes/Cube.primitive"));
	ASSERT_NE(cubeAsset, nullptr);
	Handle<MeshAsset> cube(static_cast<MeshAsset*>(cubeAsset));
	auto render = [&](MaterialAsset* material, Vec3 at, Vector<u8>& pixels) {
		Handle<Scene> scene = SceneSystem::CreateScene("Displaced");
		Handle<SceneObject> ground = SceneSystem::CreateObject("Ground", scene);
		ground->SetLocalPosition(Vec3(0.0f, -1.0f, 0.0f));
		ground->SetLocalScale(Vec3(32.0f, 0.5f, 32.0f));
		ground->AddComponent<MeshRenderer>()->SetMesh(cube);
		Handle<SceneObject> block = SceneSystem::CreateObject("Occluder", scene);
		block->SetLocalPosition(at);
		Handle<MeshRenderer> renderer = block->AddComponent<MeshRenderer>();
		renderer->SetMesh(cube);
		renderer->SetMaterial(0, Handle<MaterialAsset>(material));
		Handle<SceneObject> sun = SceneSystem::CreateObject("Sun", scene);
		sun->SetLocalRotation(Quat::FromEuler(Vec3(52.0f, 37.0f, 11.0f)));
		sun->AddComponent<Light>()->SetIntensity(2.0f);
		SceneSystem::EndFrame();
		ViewTargetId target = RenderSystem::CreateViewTarget({128, 128});
		ViewDesc desc;
		desc.view = Mat4::LookAt(Vec3(-4.0f, 9.0f, -10.0f), Vec3(1.0f, 0.0f, 0.0f));
		desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
		desc.cameraPosition = Vec3(-4.0f, 9.0f, -10.0f);
		desc.clearColor = Color::Black();
		RenderSystem::SubmitView(target, desc);
		RenderSystem::RenderAll();
		Vec2I size{};
		const bool read = RenderSystem::ReadbackViewTarget(target, pixels, size);
		const u32 lights = RenderSystem::GetStats().shadowLights;
		RenderSystem::DestroyViewTarget(target);
		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();
		return read && lights > 0;
	};

	Vector<u8> byGraph{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> byTransform{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> atRest{GetMemoryResource(MemTag::Graphics)};
	ASSERT_TRUE(render(displaced, Vec3(1.0f, 2.0f, -0.5f), byGraph));
	ASSERT_TRUE(render(resting, Vec3(3.0f, 2.0f, -0.5f), byTransform));
	ASSERT_TRUE(render(resting, Vec3(1.0f, 2.0f, -0.5f), atRest));
	u32 differing = 0;
	u32 moved = 0;
	for (size_t i = 0; i + 3 < byGraph.size(); i += 4) {
		differing += (byGraph[i] != byTransform[i] || byGraph[i + 1] != byTransform[i + 1] || byGraph[i + 2] != byTransform[i + 2]) ? 1u : 0u;
		moved += (byGraph[i] != atRest[i] || byGraph[i + 1] != atRest[i + 1] || byGraph[i + 2] != atRest[i + 2]) ? 1u : 0u;
	}
	EXPECT_EQ(differing, 0u);
	EXPECT_GT(moved, 100u);
}

TEST_F(NodeShaderVertexFixture, VertexSpacesMatchFragment) {
	const struct {
		std::string_view def;
		std::string_view space;
	} quantities[] = {
		{"Input/Position", "Object"},
		{"Input/Position", "World"},
		{"Input/Position", "View"},
		{"Input/Position", "AbsoluteWorld"},
		{"Input/NormalVector", "Object"},
		{"Input/NormalVector", "World"},
		{"Input/NormalVector", "View"},
		{"Input/TangentVector", "World"},
		{"Input/TangentVector", "Object"},
		{"Input/BitangentVector", "World"},
	};
	u32 index = 0;
	for (const auto& quantity : quantities) {
		const str label = str(quantity.def) + " " + str(quantity.space);
		GraphBuilder perVertex = UnlitStacks();
		Geometry(perVertex, 20, quantity.def, quantity.space);
		Interpolator(perVertex, 40, "Q", "Vector3");
		perVertex.Connect(20, "Out", 40, "Value");
		Reader(perVertex, 50, "Q", "Vector3");
		Encode(perVertex, 50, "Out", 70);

		GraphBuilder perPixel = UnlitStacks();
		Geometry(perPixel, 20, quantity.def, quantity.space);
		Encode(perPixel, 20, "Out", 70);

		const str vertexFile = "Vertex" + std::to_string(index) + ".slshader";
		const str pixelFile = "Pixel" + std::to_string(index) + ".slshader";
		ShaderAsset* vertexShader = AddGraph(vertexFile.c_str(), perVertex.document);
		ShaderAsset* pixelShader = AddGraph(pixelFile.c_str(), perPixel.document);
		ASSERT_NE(vertexShader, nullptr) << label;
		ASSERT_NE(pixelShader, nullptr) << label;
		MaterialAsset* vertexMaterial = LoadMaterial(("Vertex" + std::to_string(index) + ".mat").c_str(), vertexShader->assetId, "");
		MaterialAsset* pixelMaterial = LoadMaterial(("Pixel" + std::to_string(index) + ".mat").c_str(), pixelShader->assetId, "");
		Vector<u8> a{GetMemoryResource(MemTag::Graphics)};
		Vector<u8> c{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(Render(vertexMaterial, a, size)) << label;
		ASSERT_TRUE(Render(pixelMaterial, c, size)) << label;
		u32 covered = 0;
		const u32 worst = MostOff(a, c, covered);
		EXPECT_GT(covered, 200u) << label;
		EXPECT_LE(worst, 2u);
		++index;
	}
}

TEST_F(NodeShaderVertexFixture, FlatVertexIdPerTriangle) {
	auto distinct = [&](std::string_view mode, const char* fileName) {
		GraphBuilder b = UnlitStacks();
		b.Node(20, "Input/VertexID");
		b.Node(21, "Math/Multiply");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "0.2");
		b.Node(22, "Math/Add");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "0.2");
		b.Connect(20, "Out", 21, "A");
		b.Connect(21, "Out", 22, "A");
		Interpolator(b, 40, "Id", "Float", mode);
		b.Connect(22, "Out", 40, "Value");
		Reader(b, 50, "Id", "Float");
		b.Connect(50, "Out", 11, "Value");
		ShaderAsset* shader = AddGraph(fileName, b.document);
		EXPECT_NE(shader, nullptr) << fileName;
		if (!shader) {
			return size_t(0);
		}
		MaterialAsset* material = LoadMaterial((str(fileName) + ".mat").c_str(), shader->assetId, "");
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		EXPECT_TRUE(Render(material, pixels, size, false));
		std::set<u32> values;
		for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
			if (Covered(pixels, i)) {
				values.insert(pixels[i]);
			}
		}
		return values.size();
	};
	const size_t flat = distinct("NoInterpolation", "FlatId.slshader");
	const size_t ramp = distinct("Linear", "RampId.slshader");
	EXPECT_GE(flat, 1u);
	EXPECT_LE(flat, 2u);
	EXPECT_GT(ramp, 8u);
}

TEST_F(NodeShaderVertexFixture, PredisplacementReadsVertexBeforeGraphMovedIt) {
	auto graph = [&](bool predisplacement, bool subtract, const char* fileName) -> MaterialAsset* {
		GraphBuilder b = UnlitStacks();
		Geometry(b, 20, "Input/Position", "Object");
		b.Node(21, "Math/Add");
		DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.25,0,0]");
		b.Connect(20, "Out", 21, "A");
		b.Connect(21, "Out", 2, "Value");
		GraphNode& read = Geometry(b, 30, "Input/Position", "Object");
		if (predisplacement) {
			DocumentBuilder::Setting(read, "Source", Ops::Quote("Predisplacement"));
		}
		u32 source = 30;
		if (subtract) {
			b.Node(31, "Math/Subtract");
			DocumentBuilder::Constant(b.document.nodes.back(), "B", "[0.25,0,0]");
			b.Connect(30, "Out", 31, "A");
			source = 31;
		}
		Encode(b, source, "Out", 70);
		ShaderAsset* shader = AddGraph(fileName, b.document);
		EXPECT_NE(shader, nullptr) << fileName;
		return shader ? LoadMaterial((str(fileName) + ".mat").c_str(), shader->assetId, "") : nullptr;
	};
	MaterialAsset* rest = graph(true, false, "Rest.slshader");
	MaterialAsset* slidBack = graph(false, true, "SlidBack.slshader");
	MaterialAsset* slid = graph(false, false, "Slid.slshader");
	ASSERT_NE(rest, nullptr);
	ASSERT_NE(slidBack, nullptr);
	ASSERT_NE(slid, nullptr);
	Vector<u8> a{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> b{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> c{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(rest, a, size));
	ASSERT_TRUE(Render(slidBack, b, size));
	ASSERT_TRUE(Render(slid, c, size));
	u32 covered = 0;
	EXPECT_LE(MostOff(a, b, covered), 2u);
	EXPECT_GT(covered, 200u);
	EXPECT_GT(MostOff(a, c, covered), 8u);
}
