#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "GraphCompiler.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphHLSL.h"
#include "GraphProperties.h"
#include "GraphShading.h"
#include "GraphSurface.h"
#include "NodeShaderSwitch.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <format>

using namespace Shalltear;
using namespace ShadingTest;

namespace {

	GraphDocument ReadGraph(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	size_t Count(std::string_view text, std::string_view what) {
		size_t count = 0;
		for (size_t at = text.find(what); at != std::string_view::npos; at = text.find(what, at + 1)) {
			++count;
		}
		return count;
	}

	constexpr std::string_view Displaced = R"({
	"_type": "NodeShader",
	"version": 1,
	"nextId": 30,
	"settings": {"target":"lit"},
	"properties": [
		{"id":1,"name":"Tint","reference":"tint","type":"Color","default":[0,1,0,1]}
	],
	"nodes": [
		{"id":10,"def":"Procedural/GradientNoise"},
		{"id":11,"def":"Input/Position","settings":{"Space":"Object"}},
		{"id":12,"def":"Math/Add","in":{"A":{"from":[11,"Out"]},"B":{"value":[0,0.5,0]}}},
		{"id":13,"def":"Input/Property","settings":{"property":1}},
		{"id":14,"def":"CustomInterpolators/Instance","settings":{"Interpolator":"Lean","Type":"Vector4"}}
	],
	"contexts": [
		{"id":2,"def":"Context/Vertex","blocks":[
			{"id":3,"def":"Block/Position","in":{"Value":{"from":[12,"Out"]}}},
			{"id":4,"def":"Block/CustomInterpolator","settings":{"Name":"Lean","Type":"Vector4"},"in":{"Value":{"from":[13,"Out"]}}}
		]},
		{"id":5,"def":"Context/Fragment","blocks":[
			{"id":6,"def":"Block/BaseColor","in":{"Value":{"from":[10,"Out"]}}},
			{"id":7,"def":"Block/Emission","in":{"Value":{"from":[14,"Out"]}}}
		]}
	],
	"groups": [],
	"notes": []
}
)";

	str OneOutput(std::string_view output, std::string_view settings, std::string_view in, std::string_view nodes = "") {
		return std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[{}],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[]}},
			{{"id":3,"def":"{}","flow":[[1,"In"]],"settings":{},"in":{},"blocks":[]}}]}})",
						   nodes, output, settings, in);
	}

	const GraphNodeDefinition* OutputDefinition(GraphDefinitions& definitions, const GraphDocument& document) {
		for (const GraphContext& context : document.contexts) {
			if (context.node.id == 3) {
				return definitions.Find(document, context.node);
			}
		}
		return nullptr;
	}

	const GraphPortDefinition* Port(const GraphNodeDefinition& definition, std::string_view name) {
		const i32 index = GraphNodeLibrary::FindInput(definition, name);
		return index >= 0 ? &definition.inputs[static_cast<size_t>(index)] : nullptr;
	}

	const GraphSettingDefinition* Setting(const GraphNodeDefinition& definition, std::string_view key) {
		for (const GraphSettingDefinition& setting : definition.settings) {
			if (setting.key == key) {
				return &setting;
			}
		}
		return nullptr;
	}

	GraphEffect Compile(std::string_view text, Vector<GraphDiagnostic>& diagnostics, GraphDefinitions* definitions = nullptr) {
		GraphEffect effect;
		const GraphDocument document = ReadGraph(text);
		GraphEffectTarget::Compile(document, "Test.sleffect", effect, diagnostics, definitions);
		return effect;
	}

	str Said(const Vector<GraphDiagnostic>& diagnostics) {
		str all;
		for (const GraphDiagnostic& diagnostic : diagnostics) {
			all += std::format("[{}] {}\n", diagnostic.node, diagnostic.message);
		}
		return all;
	}

} // anonymous namespace

TEST(GraphEffectShadingTest, HelpersOncePerFile) {
	const GraphDocument document = ReadGraph(Displaced);
	GraphCompileRoot root;
	root.node = 6;
	root.port = "Value";
	root.input = true;
	root.stage = GraphStageFragment;
	GraphCompileRequest request;
	request.roots = {&root, 1};
	request.bindings = GraphSurface::Bindings();
	GraphProgram program;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphCompiler::Compile(document, request, program, diagnostics)) << Said(diagnostics);

	GraphHLSLText first;
	GraphHLSL::Emit(program, GraphStageAll, first);
	ASSERT_NE(first.functions.find("GradientNoise"), str::npos);
	ASSERT_NE(first.helpers.noises, 0u);
	GraphHLSLText second;
	GraphHLSL::Emit(program, GraphStageAll, second, &first.helpers);
	EXPECT_TRUE(second.functions.empty()) << second.functions;
	EXPECT_EQ(second.helpers.noises, first.helpers.noises);
	EXPECT_EQ(second.helpers.hashes, first.helpers.hashes);
	EXPECT_EQ(second.body, first.body);

	GraphHLSLText alone;
	GraphHLSL::Emit(program, GraphStageAll, alone);
	EXPECT_EQ(alone.functions, first.functions);
}

TEST(GraphEffectShadingTest, MaterialShaderHeadThenFunctions) {
	const GraphDocument document = ReadGraph(Displaced);
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	str material;
	Vector<u32> materialLines{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphSurface::Generate(document, "Displaced.slshader", material, diagnostics, nullptr, &materialLines)) << Said(diagnostics);
	GraphSurfaceFunctions functions;
	ASSERT_TRUE(GraphSurface::GenerateFunctions(document, functions, diagnostics)) << Said(diagnostics);

	ASSERT_FALSE(functions.text.empty());
	ASSERT_GE(material.size(), functions.text.size());
	EXPECT_EQ(material.substr(material.size() - functions.text.size()), functions.text);
	for (std::string_view function : {"void ShalltearVertexFunction(", "void ShalltearSurfaceFunction(", "float3 ShalltearGraphTangent(Input input)"}) {
		EXPECT_EQ(Count(functions.text, function), 1u) << function;
	}
	EXPECT_EQ(functions.text.find("cbuffer"), str::npos);
	ASSERT_GE(materialLines.size(), functions.lines.size());
	EXPECT_TRUE(std::equal(functions.lines.begin(), functions.lines.end(), materialLines.end() - static_cast<std::ptrdiff_t>(functions.lines.size())));
	EXPECT_NE(std::count(functions.lines.begin(), functions.lines.end(), 12u), 0);

	EXPECT_EQ(functions.target, GraphSurfaceFunctions::Target::Lit);
	EXPECT_TRUE(functions.vertexFunction);
	EXPECT_EQ(functions.interpolators[0], 1u);
	ASSERT_EQ(functions.properties.size(), 1u);
	EXPECT_EQ(functions.propertyStages[0] & GraphStageVertex, GraphStageVertex);
	EXPECT_EQ(functions.propertyStages[0] & GraphStageFragment, 0u);
	EXPECT_FALSE(functions.fragmentReadsObject);
	EXPECT_FALSE(functions.supportsEffects);
}

TEST(GraphEffectShadingTest, SupportEffectsOffByDefault) {
	GraphDocument document = ReadGraph(Displaced);
	EXPECT_FALSE(GraphSurface::SupportsEffects(document));
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	str before;
	ASSERT_TRUE(GraphSurface::Generate(document, "Displaced.slshader", before, diagnostics)) << Said(diagnostics);
	document.settings.push_back({str("supportEffects"), str("true")});
	EXPECT_TRUE(GraphSurface::SupportsEffects(document));
	str after;
	ASSERT_TRUE(GraphSurface::Generate(document, "Displaced.slshader", after, diagnostics)) << Said(diagnostics);
	EXPECT_EQ(after, before);
	GraphSurfaceFunctions functions;
	ASSERT_TRUE(GraphSurface::GenerateFunctions(document, functions, diagnostics));
	EXPECT_TRUE(functions.supportsEffects);

	for (GraphSetting& setting : document.settings) {
		if (setting.key == "target") {
			setting.value = "\"fullscreen\"";
		}
	}
	EXPECT_FALSE(GraphSurface::SupportsEffects(document));
	diagnostics.clear();
	EXPECT_FALSE(GraphSurface::GenerateFunctions(document, functions, diagnostics));
	EXPECT_NE(Said(diagnostics).find("Fullscreen"), str::npos) << Said(diagnostics);
}

TEST(GraphEffectShadingTest, HostDeclaresPropertiesAsValues) {
	const GraphDocument document = ReadGraph(R"({"_type":"NodeShader","version":1,"nextId":10,"settings":{},"properties":[
		{"id":1,"name":"Tint","reference":"tint","type":"Color","default":[0,1,0.5,1]},
		{"id":2,"name":"Steps","reference":"steps","type":"Float","default":[3],"settings":{"mode":"integer"}},
		{"id":3,"name":"Rim","reference":"rim","type":"Boolean","default":[1]},
		{"id":4,"name":"Map","reference":"map","type":"Texture2D"}],
		"nodes":[],"contexts":[],"groups":[],"notes":[]})");
	Vector<GraphPropertyInfo> properties{GetMemoryResource(MemTag::Editor)};
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphProperties::ParseBlackboard(document, properties, diagnostics)) << Said(diagnostics);
	const str declared = GraphProperties::HostDeclarations(properties, 4);
	EXPECT_NE(declared.find("static float4 tint = float4(0.0, 1.0, 0.5, 1.0);"), str::npos) << declared;
	EXPECT_NE(declared.find("static int steps = 3;"), str::npos) << declared;
	EXPECT_NE(declared.find("static int rim = 1;"), str::npos) << declared;
	EXPECT_NE(declared.find("[[vk::binding(4, 1)]] Texture2D map;"), str::npos) << declared;
	EXPECT_NE(declared.find("[[vk::binding(5, 1)]] SamplerState " + GraphProperties::SamplerName(properties[3]) + ";"), str::npos) << declared;
	EXPECT_EQ(declared.find("cbuffer"), str::npos) << declared;
}

TEST(GraphEffectShadingTest, NewOutputShadesWithDefaultGraph) {
	for (std::string_view def : {GRAPH_EFFECT_OUTPUT_NODE_SHADER, GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH}) {
		SCOPED_TRACE(def);
		const GraphDocument document = ReadGraph(OneOutput(def, "{}", "{}"));
		GraphDefinitions definitions;
		const GraphNodeDefinition* definition = OutputDefinition(definitions, document);
		ASSERT_NE(definition, nullptr);
		EXPECT_TRUE(GraphEffectRules::IsOutput(def));
		const GraphPortDefinition* color = Port(*definition, "_Color");
		const GraphPortDefinition* map = Port(*definition, "_BaseMap");
		ASSERT_NE(color, nullptr);
		ASSERT_NE(map, nullptr);
		EXPECT_EQ(color->type, GraphPortType::Float4);
		EXPECT_EQ(color->defaultValue[0], 1.0f);
		EXPECT_EQ(color->defaultValue[3], 1.0f);
		EXPECT_EQ(map->type, GraphPortType::Texture2D);
		EXPECT_FALSE(map->binding.empty());
		const GraphSettingDefinition* graph = Setting(*definition, GRAPH_EFFECT_NODE_SHADER_SETTING);
		ASSERT_NE(graph, nullptr);
		EXPECT_EQ(graph->kind, GraphSettingKind::Asset);
		EXPECT_TRUE(graph->inGraph);
		EXPECT_EQ(Setting(*definition, "Receive Shadows"), nullptr);
		EXPECT_EQ(Setting(*definition, "Blend Mode"), nullptr);
		const GraphSettingDefinition* mode = Setting(*definition, "Sort Mode");
		ASSERT_NE(mode, nullptr);
		EXPECT_NE(mode->whenOptions & 1u, 0u);
	}
}

TEST(GraphEffectShadingTest, FedPortIsParticleValue) {
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const GraphEffect fresh = Compile(OneOutput(GRAPH_EFFECT_OUTPUT_NODE_SHADER, "{}", "{}"), diagnostics);
	ASSERT_TRUE(diagnostics.empty()) << Said(diagnostics);
	ASSERT_EQ(fresh.systems.size(), 1u);
	ASSERT_EQ(fresh.systems[0].outputs.size(), 1u);
	const GraphEffectOutput& output = fresh.systems[0].outputs[0];
	EXPECT_EQ(output.blend, RHI::BlendMode::Alpha);
	EXPECT_EQ(output.sort, GraphEffectSort::DistanceToCamera);
	EXPECT_EQ(Count(output.shader, "void ShalltearSurfaceFunction("), 1u);
	EXPECT_NE(output.shader.find("static float4 _Color"), str::npos);
	EXPECT_NE(output.shader.find("#pragma shalltear_blend alpha"), str::npos);
	EXPECT_NE(output.shader.find("#include \"VisualEffects/ParticleGraph.hlsli\""), str::npos);
	EXPECT_EQ(output.shader.find("properties["), str::npos);
	ASSERT_EQ(output.textures.size(), 1u);
	EXPECT_EQ(output.textures[0].slot, "_BaseMap");
	EXPECT_FALSE(output.textures[0].texture.IsValid());

	const GraphEffect fed = Compile(OneOutput(GRAPH_EFFECT_OUTPUT_NODE_SHADER, "{}", R"({"_Color":{"from":[20,"color"]}})",
											  R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"color"}})"),
									diagnostics);
	ASSERT_TRUE(diagnostics.empty()) << Said(diagnostics);
	const str& shader = fed.systems[0].outputs[0].shader;
	EXPECT_NE(shader.find("nointerpolation float4 properties[1]"), str::npos) << shader;
	EXPECT_NE(shader.find("sltOut.properties[0]"), str::npos);
	EXPECT_NE(shader.find("sltIn.properties[0]"), str::npos);
	EXPECT_NE(shader.find("SltGraphProperties("), str::npos);
}

TEST(GraphEffectShadingTest, ClearedOrMissingNodeShaderIsRefused) {
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	Compile(OneOutput(GRAPH_EFFECT_OUTPUT_NODE_SHADER, R"({"Node Shader":null})", "{}"), diagnostics);
	EXPECT_NE(Said(diagnostics).find("has no Node Shader"), str::npos) << Said(diagnostics);
	diagnostics.clear();
	Compile(OneOutput(GRAPH_EFFECT_OUTPUT_NODE_SHADER, R"({"Node Shader":"0123456789abcdef-0123456789abcdef"})", "{}"), diagnostics);
	EXPECT_NE(Said(diagnostics).find("doesn't have"), str::npos) << Said(diagnostics);
}

namespace {

	class GraphEffectShadingFixture : public MaterialShadingFixture {
	protected:
		Guid AddGraph(const char* name, std::string_view text) {
			WriteProjectFile(name, str(text));
			return AssetDatabase::EnsureAssetIdentity(Path(str("project://") + name));
		}

		str Shaded(const Guid& graph, std::string_view settings = "") {
			const str stored = std::format(R"({{"Node Shader":"{}"{}}})", graph.ToString(), settings);
			return OneOutput(GRAPH_EFFECT_OUTPUT_NODE_SHADER, stored, "{}");
		}
	};

	constexpr std::string_view Blackboard = R"({"_type":"NodeShader","version":1,"nextId":30,
		"settings":{"target":"lit","supportEffects":true},
		"properties":[
			{"id":1,"name":"Tint","reference":"tint","type":"Color","default":[0,1,0,1]},
			{"id":2,"name":"Secret","reference":"secret","type":"Float","default":[2],"settings":{"hidden":true}},
			{"id":3,"name":"Use Rim","reference":"_USE_RIM","type":"BooleanKeyword","default":[1]},
			{"id":4,"name":"Quality","reference":"_QUALITY","type":"EnumKeyword","default":[1],
				"settings":{"entries":[{"id":1,"name":"Low","reference":"LOW"},{"id":2,"name":"High","reference":"HIGH"}]}}],
		"nodes":[{"id":10,"def":"Input/Property","settings":{"property":1}}],
		"contexts":[{"id":5,"def":"Context/Fragment","blocks":[{"id":6,"def":"Block/BaseColor","in":{"Value":{"from":[10,"Out"]}}}]}],
		"groups":[],"notes":[]})";

} // anonymous namespace

TEST_F(GraphEffectShadingFixture, GraphsBlackboardIsOutputsPortsAndSettings) {
	const Guid graph = AddGraph("Blackboard.slshader", Blackboard);
	ASSERT_TRUE(graph.IsValid());
	const GraphDocument document = ReadGraph(Shaded(graph));
	GraphDefinitions definitions;
	const GraphNodeDefinition* definition = OutputDefinition(definitions, document);
	ASSERT_NE(definition, nullptr);
	EXPECT_NE(Port(*definition, "tint"), nullptr);
	EXPECT_EQ(Port(*definition, "secret"), nullptr);
	EXPECT_EQ(Port(*definition, "_BaseMap"), nullptr);
	const GraphSettingDefinition* rim = Setting(*definition, "Use Rim");
	ASSERT_NE(rim, nullptr);
	EXPECT_EQ(rim->kind, GraphSettingKind::Toggle);
	EXPECT_EQ(rim->defaultIndex, 1u);
	const GraphSettingDefinition* quality = Setting(*definition, "Quality");
	ASSERT_NE(quality, nullptr);
	EXPECT_EQ(quality->kind, GraphSettingKind::Choice);
	ASSERT_EQ(quality->options.size(), 2u);
	EXPECT_EQ(quality->options[1], "High");
	EXPECT_EQ(quality->defaultIndex, 1u);
	EXPECT_NE(Setting(*definition, "Receive Shadows"), nullptr);
	const GraphSettingDefinition* mode = Setting(*definition, "Sort Mode");
	ASSERT_NE(mode, nullptr);
	EXPECT_EQ(mode->whenOptions, 1u << 2);
	const GraphPortDefinition* threshold = Port(*definition, "Alpha Threshold");
	ASSERT_NE(threshold, nullptr);
	EXPECT_EQ(threshold->orSetting, GRAPH_PORT_ALWAYS);

	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const GraphEffect defaults = Compile(Shaded(graph), diagnostics);
	ASSERT_TRUE(diagnostics.empty()) << Said(diagnostics);
	const str& shader = defaults.systems[0].outputs[0].shader;
	EXPECT_NE(shader.find("#define _USE_RIM\n"), str::npos);
	EXPECT_NE(shader.find("#define _QUALITY_HIGH\n"), str::npos);
	EXPECT_NE(shader.find("static float secret = 2.0;"), str::npos);
	EXPECT_EQ(defaults.systems[0].outputs[0].sort, GraphEffectSort::None);
	EXPECT_NE(shader.find("ParticleLit.hlsli"), str::npos);

	const GraphEffect chosen = Compile(Shaded(graph, R"(,"Use Rim":false,"Quality":"Low")"), diagnostics);
	ASSERT_TRUE(diagnostics.empty()) << Said(diagnostics);
	EXPECT_EQ(chosen.systems[0].outputs[0].shader.find("#define _USE_RIM"), str::npos);
	EXPECT_NE(chosen.systems[0].outputs[0].shader.find("#define _QUALITY_LOW\n"), str::npos);
}

TEST_F(GraphEffectShadingFixture, OutputErrorRefusedByName) {
	str off(Blackboard);
	off.replace(off.find(R"(,"supportEffects":true)"), std::string_view(R"(,"supportEffects":true)").size(), "");
	str fullscreen(Blackboard);
	fullscreen.replace(fullscreen.find(R"("target":"lit")"), std::string_view(R"("target":"lit")").size(), R"("target":"fullscreen")");
	str sky(Blackboard);
	sky.replace(sky.find(R"("target":"lit")"), std::string_view(R"("target":"lit")").size(), R"("target":"sky")");
	str broken(Blackboard);
	broken.replace(broken.find(R"({"id":10,"def":"Input/Property","settings":{"property":1}})"),
				   std::string_view(R"({"id":10,"def":"Input/Property","settings":{"property":1}})").size(),
				   R"({"id":10,"def":"Input/Property","settings":{"property":99}})");
	const struct {
		const char* file;
		str text;
		std::string_view said;
	} cases[] = {
		{"Off.slshader", off, "Support Effects is off"},
		{"Fullscreen.slshader", fullscreen, "Fullscreen"},
		{"Sky.slshader", sky, "(a Sky graph shades the sky, not a particle)"},
		{"Sub.slsubgraph", R"({"_type":"ShaderSubgraph","version":1,"nextId":2,"settings":{},"properties":[],"nodes":[],"contexts":[],"groups":[],"notes":[]})",
		 "not a node shader"},
		{"Broken.slshader", broken, "(its node 10"},
	};
	for (const auto& entry : cases) {
		SCOPED_TRACE(entry.file);
		const Guid graph = AddGraph(entry.file, entry.text);
		ASSERT_TRUE(graph.IsValid());
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		Compile(Shaded(graph), diagnostics);
		EXPECT_NE(Said(diagnostics).find(entry.said), str::npos) << Said(diagnostics);
	}
}

TEST_F(GraphEffectShadingFixture, CompileListsGraphItReadAsDependency) {
	const Guid graph = AddGraph("Blackboard.slshader", Blackboard);
	ASSERT_TRUE(graph.IsValid());
	GraphDefinitions definitions;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	Compile(Shaded(graph), diagnostics, &definitions);
	ASSERT_TRUE(diagnostics.empty()) << Said(diagnostics);
	Vector<const GraphReadDocument*> read{GetMemoryResource(MemTag::Editor)};
	definitions.Read(read);
	ASSERT_EQ(read.size(), 1u);
	EXPECT_EQ(read[0]->asset, graph);
	EXPECT_EQ(read[0]->physicalPath, FileSystem::Resolve(Path("project://Blackboard.slshader")).ToString());
	EXPECT_NE(read[0]->contentHash, 0u);

	GraphDefinitions defaults;
	Compile(OneOutput(GRAPH_EFFECT_OUTPUT_NODE_SHADER, "{}", "{}"), diagnostics, &defaults);
	ASSERT_TRUE(diagnostics.empty()) << Said(diagnostics);
	defaults.Read(read);
	EXPECT_TRUE(read.empty());
}

namespace {

	bool Shades(const GraphDocument&, std::string_view, str&) { return true; }
	bool Generates(const GraphDocument&, GraphSurfaceFunctions&, Vector<GraphDiagnostic>&, GraphDefinitions*) { return true; }
	bool Refuses(const GraphDocument&, std::string_view, str& why) {
		why = "refused";
		return false;
	}

} // anonymous namespace

TEST(GraphEffectShadingTest, RegistryKeepsFirstOwner) {
	const int first = 0;
	const int second = 0;
	const GraphShadingTarget shades{&Shades, &Generates};
	const GraphShadingTarget refuses{&Refuses, &Generates};
	ASSERT_EQ(GraphShadingTargets::Find("TestShadingA"), nullptr);

	EXPECT_TRUE(GraphShadingTargets::Register("TestShadingA", shades, &first));
	EXPECT_FALSE(GraphShadingTargets::Register("TestShadingA", refuses, &second));
	EXPECT_TRUE(GraphShadingTargets::Register("TestShadingB", refuses, &second));
	EXPECT_FALSE(GraphShadingTargets::Register("TestShadingC", shades, nullptr));
	EXPECT_FALSE(GraphShadingTargets::Register("TestShadingC", GraphShadingTarget{&Shades, nullptr}, &first));
	EXPECT_EQ(GraphShadingTargets::Find("TestShadingC"), nullptr);

	const GraphShadingTarget* a = GraphShadingTargets::Find("TestShadingA");
	ASSERT_NE(a, nullptr);
	EXPECT_EQ(a->canShadeEffects, &Shades);
	EXPECT_NE(GraphShadingTargets::Find(GRAPH_SHADER_DOCUMENT_TYPE), nullptr);

	const u64 withA = GraphShadingTargets::Fingerprint();
	EXPECT_FALSE(GraphShadingTargets::Register("TestShadingA", refuses, &second));
	EXPECT_EQ(GraphShadingTargets::Fingerprint(), withA);

	EXPECT_EQ(GraphShadingTargets::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_EQ(GraphShadingTargets::UnregisterOwnedBy(&first), 1u);
	EXPECT_NE(GraphShadingTargets::Fingerprint(), withA);
	EXPECT_EQ(GraphShadingTargets::Find("TestShadingA"), nullptr);
	EXPECT_NE(GraphShadingTargets::Find("TestShadingB"), nullptr);
	EXPECT_NE(GraphShadingTargets::Find(GRAPH_SHADER_DOCUMENT_TYPE), nullptr);
	EXPECT_EQ(GraphShadingTargets::UnregisterOwnedBy(&second), 1u);
	EXPECT_EQ(GraphShadingTargets::Find("TestShadingB"), nullptr);
}

TEST_F(GraphEffectShadingFixture, NodeShaderOffRefusesGraph) {
	const Guid graph = AddGraph("Blackboard.slshader", Blackboard);
	ASSERT_TRUE(graph.IsValid());
	const str shaded = OneOutput(GRAPH_EFFECT_OUTPUT_NODE_SHADER, "{}", "{}");
	const str plain = OneOutput(GRAPH_EFFECT_OUTPUT, "{}", "{}");
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	Compile(shaded, diagnostics);
	Compile(Shaded(graph), diagnostics);
	ASSERT_TRUE(diagnostics.empty()) << Said(diagnostics);

	{
		const NodeShaderTesting::NodeShaderSwitchedOff off;
		ASSERT_TRUE(off.HadTarget());
		ASSERT_EQ(GraphShadingTargets::Find(GRAPH_SHADER_DOCUMENT_TYPE), nullptr);

		const GraphEffect refused = Compile(shaded, diagnostics);
		EXPECT_TRUE(refused.systems.empty());
		EXPECT_NE(Said(diagnostics).find("shades with node shader 'EffectDefault' (Node Shader is off). Turn it on in the Package Manager"), str::npos)
			<< Said(diagnostics);
		diagnostics.clear();
		Compile(Shaded(graph), diagnostics);
		EXPECT_NE(Said(diagnostics).find("shades with node shader 'Blackboard' (Node Shader is off)"), str::npos) << Said(diagnostics);
		diagnostics.clear();

		const GraphEffect other = Compile(plain, diagnostics);
		EXPECT_TRUE(diagnostics.empty()) << Said(diagnostics);
		ASSERT_EQ(other.systems.size(), 1u);
		EXPECT_EQ(other.systems[0].outputs.size(), 1u);
	}

	ASSERT_NE(GraphShadingTargets::Find(GRAPH_SHADER_DOCUMENT_TYPE), nullptr);
	const GraphEffect again = Compile(Shaded(graph), diagnostics);
	EXPECT_TRUE(diagnostics.empty()) << Said(diagnostics);
	EXPECT_EQ(again.systems.size(), 1u);
}
