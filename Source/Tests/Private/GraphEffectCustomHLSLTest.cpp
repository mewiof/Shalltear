#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "GraphCompiler.h"
#include "GraphCustomHLSL.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphNodeLibrary.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>

#ifndef SHALLTEAR_SHARED
#include "EffectKernelHarness.h"
#endif

using namespace Shalltear;

namespace {

	GraphDocument Parse(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	str Json(std::string_view text) {
		str out = "\"";
		for (char c : text) {
			if (c == '"' || c == '\\') {
				out += '\\';
				out += c;
			} else if (c == '\n') {
				out += "\\n";
			} else {
				out += c;
			}
		}
		return out + "\"";
	}

	str Block(u32 id, std::string_view code, std::string_view in = {}, std::string_view settings = {}) {
		return std::format(R"({{"id":{},"def":"Block/CustomHLSL","settings":{{"HLSL Code":{}{}}}{}}})", id, Json(code), settings,
						   in.empty() ? str() : ",\"in\":{" + str(in) + "}");
	}

	str Effect(std::string_view initialize, std::string_view nodes = {}, std::string_view update = {}, std::string_view output = {}) {
		return std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[{}],"contexts":[
{{"id":1,"def":"Context/Initialize","settings":{{"Capacity":64}},"blocks":[{}]}},
{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[{}]}}]}})",
						   nodes, initialize, update, output);
	}

	constexpr std::string_view LIFT = "void Lift(inout EffectAttributes attributes, in float3 offset, float scale)\n"
									  "{\n"
									  "  attributes.position += offset;\n"
									  "  attributes.size = scale * 2;\n"
									  "}";

	class GraphEffectCustomHLSLFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() / ("shalltear_effect_hlsl_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid Add(const char* relative, std::string_view text) {
			const std::filesystem::path file = _root / relative;
			std::filesystem::create_directories(file.parent_path());
			FileSystem::WriteTextFile(Path(file.generic_string()), str(text));
			return AssetDatabase::EnsureAssetIdentity(Path(str("project://") + relative));
		}

		static bool Compile(const str& text, GraphEffect& out, Vector<GraphDiagnostic>& diagnostics, std::string_view source = "CustomHLSL") {
			GraphDefinitions definitions;
			return GraphEffectTarget::Compile(Parse(text), source, out, diagnostics, &definitions);
		}

		static str Said(const Vector<GraphDiagnostic>& diagnostics) {
			str all;
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				all += std::format("[node {}] {}\n", diagnostic.node, diagnostic.message);
			}
			return all;
		}

		static str RefusalOf(std::string_view block) {
			GraphEffect effect;
			Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
			EXPECT_FALSE(Compile(Effect(block), effect, diagnostics));
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				if (diagnostic.node == 50) {
					return diagnostic.message;
				}
			}
			ADD_FAILURE() << "nothing said on the block: " << Said(diagnostics);
			return {};
		}

		static size_t Count(std::string_view text, std::string_view what) {
			size_t count = 0;
			for (size_t at = text.find(what); at != std::string_view::npos; at = text.find(what, at + what.size())) {
				++count;
			}
			return count;
		}
	};

} // anonymous namespace

TEST(GraphCustomHLSLTest, FunctionFoundByReturnType) {
	constexpr std::string_view CODE = "#include \"Common.hlsl\"\n"
									  "static const float K = 2;\n"
									  "/// Hidden\n"
									  "float Helper(float x) { return x * K; }\n"
									  "/// a: the first\n"
									  "/// return: sum\n"
									  "float Add(in float3 a, out float b, inout uint c, StructuredBuffer<float3> d)\n"
									  "{\n"
									  "  return a.x;\n"
									  "}\n"
									  "  void Indented(float x) {}\n"
									  "half NotKnown(float x) {}\n"
									  "void Last (inout EffectAttributes attributes)\n"
									  "{ }\n";
	Vector<GraphCustomHLSL::Function> functions{GetMemoryResource(MemTag::Editor)};
	GraphCustomHLSL::Parse(CODE, functions);
	ASSERT_EQ(functions.size(), 3u);
	EXPECT_EQ(functions[0].name, "Helper");
	EXPECT_TRUE(functions[0].hidden);
	EXPECT_EQ(functions[0].body, "{ return x * K; }");

	const GraphCustomHLSL::Function& add = functions[1];
	EXPECT_EQ(add.name, "Add");
	EXPECT_EQ(add.returnType, "float");
	EXPECT_FALSE(add.hidden);
	EXPECT_EQ(add.returnName, "sum");
	ASSERT_EQ(add.parameters.size(), 4u);
	EXPECT_EQ(add.parameters[0].access, GraphCustomHLSL::Access::In);
	EXPECT_EQ(add.parameters[0].type, "float3");
	EXPECT_EQ(add.parameters[0].name, "a");
	EXPECT_EQ(add.parameters[1].access, GraphCustomHLSL::Access::Out);
	EXPECT_EQ(add.parameters[2].access, GraphCustomHLSL::Access::InOut);
	EXPECT_EQ(add.parameters[2].type, "uint");
	EXPECT_EQ(add.parameters[3].access, GraphCustomHLSL::Access::None);
	EXPECT_EQ(add.parameters[3].type, "StructuredBuffer");
	EXPECT_EQ(add.parameters[3].templateType, "float3");
	EXPECT_EQ(add.parameters[3].name, "d");
	EXPECT_TRUE(add.body.starts_with("{\n  return a.x;\n}")) << add.body;
	EXPECT_EQ(add.body.find("Last"), str::npos);

	EXPECT_EQ(functions[2].name, "Last");
	EXPECT_EQ(functions[2].returnName, "out");
	EXPECT_EQ(functions[2].body, "{ }");
}

TEST(GraphCustomHLSLTest, AttributeAccessReadFromCode) {
	constexpr std::string_view BODY = "{\n"
									  "  a.position = float3(0, 0, 0);\n"
									  "  a.velocity += 1;\n"
									  "  a.age++;\n"
									  "  --a.lifetime;\n"
									  "  a.color.x = 1;\n"
									  "  a.axisX[1] = 0;\n"
									  "  if (a.alpha == a.mass && a.size <= 1) {}\n"
									  "  a.texIndex <<= 1;\n"
									  "  // a.pivotX = 3;\n"
									  "  /* a.scaleX = 3; */\n"
									  "  float other = data.angleX + a . direction.y;\n"
									  "}";
	Vector<GraphCustomHLSL::FieldAccess> accesses{GetMemoryResource(MemTag::Editor)};
	GraphCustomHLSL::Accesses(BODY, "a", accesses);
	const auto find = [&](std::string_view field) -> const GraphCustomHLSL::FieldAccess* {
		for (const GraphCustomHLSL::FieldAccess& access : accesses) {
			if (access.field == field) {
				return &access;
			}
		}
		return nullptr;
	};
	const struct {
		std::string_view field;
		bool read;
		bool written;
	} expected[] = {{"position", false, true}, {"velocity", true, true}, {"age", true, true}, {"lifetime", true, true}, {"color", true, true}, {"axisX", true, true}, {"alpha", true, false}, {"mass", true, false}, {"size", true, false}, {"texIndex", true, true}, {"direction", true, false}};
	for (const auto& want : expected) {
		const GraphCustomHLSL::FieldAccess* access = find(want.field);
		ASSERT_NE(access, nullptr) << want.field;
		EXPECT_EQ(access->read, want.read) << want.field;
		EXPECT_EQ(access->written, want.written) << want.field;
	}
	EXPECT_EQ(find("pivotX"), nullptr);
	EXPECT_EQ(find("scaleX"), nullptr);
	EXPECT_EQ(find("angleX"), nullptr);
	EXPECT_EQ(accesses.size(), std::size(expected));

	Vector<str> includes{GetMemoryResource(MemTag::Editor)};
	GraphCustomHLSL::Includes("#include \"A.hlsl\"\nvoid F() {}\n#include \"Sub/B.hlsl\"\n#include \"A.hlsl\"\n  #include \"C.hlsl\"\n", includes);
	ASSERT_EQ(includes.size(), 2u);
	EXPECT_EQ(includes[0], "A.hlsl");
	EXPECT_EQ(includes[1], "Sub/B.hlsl");
}

TEST_F(GraphEffectCustomHLSLFixture, BlockPortsFromParameters) {
	constexpr std::string_view CODE = "/// Hidden\n"
									  "void Helper(inout EffectAttributes attributes) {}\n"
									  "void Paint(inout EffectAttributes attributes, in float4 tint, bool on)\n"
									  "{\n"
									  "  if (on) attributes.color = tint.rgb * attributes.alpha;\n"
									  "  attributes.position.y += 1;\n"
									  "}\n"
									  "void Paint(inout EffectAttributes attributes) {}\n"
									  "void Other(inout EffectAttributes attributes) { attributes.size = 2; }\n";
	const GraphDocument document = Parse(Effect(Block(50, CODE)));
	GraphDefinitions definitions;
	const GraphNode& node = document.contexts[0].blocks[0];
	const GraphNodeDefinition* definition = definitions.Find(document, node);
	ASSERT_NE(definition, nullptr);
	EXPECT_EQ(definition->name, "Custom HLSL");
	ASSERT_EQ(definition->inputs.size(), 2u);
	EXPECT_EQ(definition->inputs[0].name, "tint");
	EXPECT_EQ(definition->inputs[0].type, GraphPortType::Float4);
	EXPECT_EQ(definition->inputs[1].name, "on");
	EXPECT_EQ(definition->inputs[1].type, GraphPortType::Boolean);
	ASSERT_EQ(definition->outputs.size(), 2u);
	EXPECT_EQ(definition->outputs[0].name, "position");
	EXPECT_EQ(definition->outputs[1].name, "color");
	ASSERT_NE(definition->customHLSL, nullptr);
	EXPECT_TRUE(definition->customHLSL->error.empty()) << definition->customHLSL->error;
	ASSERT_EQ(definition->customHLSL->attributes.size(), 3u);
	EXPECT_EQ(definition->customHLSL->attributes[0], "position");
	EXPECT_EQ(definition->customHLSL->attributes[1], "color");
	EXPECT_EQ(definition->customHLSL->attributes[2], "alpha");

	const GraphSettingDefinition* available = nullptr;
	for (const GraphSettingDefinition& setting : definition->settings) {
		available = setting.key == GRAPH_CUSTOM_HLSL_FUNCTION ? &setting : available;
	}
	ASSERT_NE(available, nullptr);
	ASSERT_EQ(available->options.size(), 2u);
	EXPECT_EQ(available->options[0], "Paint");
	EXPECT_EQ(available->options[1], "Other");
	EXPECT_TRUE(available->inGraph);

	GraphDocument chosen = Parse(Effect(Block(50, CODE, {}, R"(,"Available Function":"Other","Block Name":"Mine")")));
	const GraphNodeDefinition* other = definitions.Find(chosen, chosen.contexts[0].blocks[0]);
	ASSERT_NE(other, nullptr);
	EXPECT_EQ(other->name, "Mine");
	ASSERT_EQ(other->outputs.size(), 1u);
	EXPECT_EQ(other->outputs[0].name, "size");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_TRUE(Compile(Effect(Block(50, CODE, {}, R"(,"Available Function":"Gone")")), effect, diagnostics)) << Said(diagnostics);
}

TEST_F(GraphEffectCustomHLSLFixture, OperatorPortsFromSignature) {
	constexpr std::string_view CODE = "/// return: sum\n"
									  "float Mix(in float3 a, out float2 b, uint n, inout bool c, float4x4 m)\n"
									  "{\n"
									  "  b = a.xy; c = true;\n"
									  "  return a.x + n;\n"
									  "}";
	const GraphDocument document =
		Parse(Effect({}, std::format(R"({{"id":40,"def":"HLSL/CustomHLSL","settings":{{"HLSL Code":{},"Operator Name":"Mixer"}}}})", Json(CODE))));
	GraphDefinitions definitions;
	const GraphNodeDefinition* definition = definitions.Find(document, document.nodes[0]);
	ASSERT_NE(definition, nullptr);
	EXPECT_EQ(definition->name, "Mixer");
	ASSERT_EQ(definition->inputs.size(), 3u);
	EXPECT_EQ(definition->inputs[0].name, "a");
	EXPECT_EQ(definition->inputs[1].name, "n");
	EXPECT_EQ(definition->inputs[1].type, GraphPortType::Float);
	EXPECT_EQ(definition->inputs[2].name, "m");
	EXPECT_EQ(definition->inputs[2].type, GraphPortType::Float4x4);
	ASSERT_EQ(definition->outputs.size(), 3u);
	EXPECT_EQ(definition->outputs[0].name, "b");
	EXPECT_EQ(definition->outputs[0].type, GraphPortType::Float2);
	EXPECT_EQ(definition->outputs[1].name, "c");
	EXPECT_EQ(definition->outputs[1].type, GraphPortType::Boolean);
	EXPECT_EQ(definition->outputs[2].name, "sum");
	ASSERT_NE(definition->customHLSL, nullptr);
	EXPECT_TRUE(definition->customHLSL->error.empty()) << definition->customHLSL->error;
	EXPECT_TRUE(definition->customHLSL->attributes.empty());

	const GraphNodeDefinition* table = GraphNodeLibrary::Find(GRAPH_EFFECT_CUSTOM_HLSL_OPERATOR);
	ASSERT_NE(table, nullptr);
	ASSERT_EQ(table->inputs.size(), 1u);
	EXPECT_EQ(table->inputs[0].name, "value");
	ASSERT_EQ(table->outputs.size(), 1u);
	EXPECT_EQ(table->outputs[0].name, "out");
	GraphDocument dropped = Parse(Effect({}, R"({"id":40,"def":"HLSL/CustomHLSL"})"));
	const GraphNodeDefinition* fresh = definitions.Find(dropped, dropped.nodes[0]);
	ASSERT_NE(fresh, nullptr);
	ASSERT_EQ(fresh->inputs.size(), 1u);
	EXPECT_EQ(fresh->inputs[0].name, "value");
	EXPECT_TRUE(fresh->customHLSL->error.empty());
}

TEST_F(GraphEffectCustomHLSLFixture, UnrunnableCodeRefusedOnNode) {
	const struct {
		std::string_view code;
		std::string_view said;
	} cases[] = {
		{"float F(inout EffectAttributes attributes) { return 1; }", "must return void"},
		{"void F(float x) {}", "takes no EffectAttributes"},
		{"void F(inout EffectAttributes attributes, out float x) { x = 1; }", "`x` is out or inout"},
		{"void F(inout EffectAttributes attributes) { attributes.nothing = 1; }", "`attributes.nothing` (not an attribute)"},
		{"void F(inout EffectAttributes attributes) { attributes.scale = 1; }", "scaleX, scaleY and scaleZ"},
		{"void F(inout EffectAttributes attributes) { attributes.particleId = 1; }", "`attributes.particleId` (read-only)"},
		{"void F(in EffectAttributes attributes) { attributes.size = 1; }", "EffectAttributes that isn't inout"},
		{"void F(inout EffectAttributes attributes, EffectGradient g) {}", "`g` is a EffectGradient (a Custom HLSL node can't be passed one yet)"},
		{"void F(inout EffectAttributes attributes, half h) {}", "`h` has the unknown type 'half'"},
		{"// nothing here", "declares no HLSL function a block can run"},
	};
	for (const auto& one : cases) {
		const str said = RefusalOf(Block(50, one.code));
		EXPECT_NE(said.find(one.said), str::npos) << "for `" << one.code << "` it said: " << said;
	}
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const auto operatorRefusal = [&](std::string_view code) {
		const str text = Effect(R"({"id":50,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[40,"out"]}}})",
								std::format(R"({{"id":40,"def":"HLSL/CustomHLSL","settings":{{"HLSL Code":{}}}}})", Json(code)));
		EXPECT_FALSE(Compile(text, effect, diagnostics));
		return Said(diagnostics);
	};
	EXPECT_NE(operatorRefusal("float F(inout EffectAttributes attributes) { return 1; }").find("[node 40] calls `F`. It takes EffectAttributes"), str::npos);
	EXPECT_NE(operatorRefusal("void F(float x) {}").find("[node 40] calls `F`. It returns nothing + writes no out parameter"), str::npos);

	GraphDocument broken = Parse(Effect(Block(50, "void F(float x) {}")));
	GraphDefinitions definitions;
	const GraphNodeDefinition* definition = definitions.Find(broken, broken.contexts[0].blocks[0]);
	ASSERT_NE(definition, nullptr);
	EXPECT_TRUE(std::any_of(definition->settings.begin(), definition->settings.end(),
							[](const GraphSettingDefinition& setting) { return setting.key == GRAPH_CUSTOM_HLSL_CODE && setting.multiline; }));
}

TEST_F(GraphEffectCustomHLSLFixture, BlockFitsParticleContexts) {
	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_CUSTOM_HLSL_BLOCK));
	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_UPDATE, GRAPH_EFFECT_CUSTOM_HLSL_BLOCK));
	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_CUSTOM_HLSL_BLOCK));
	EXPECT_TRUE(GraphEffectRules::Holds(GRAPH_EFFECT_OUTPUT_MESH, GRAPH_EFFECT_CUSTOM_HLSL_BLOCK));
	EXPECT_FALSE(GraphEffectRules::Holds(GRAPH_EFFECT_SPAWN, GRAPH_EFFECT_CUSTOM_HLSL_BLOCK));

	const GraphDocument document = Parse(Effect({}));
	const auto offered = [&](u32 context) {
		Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		EffectGraphEditOps::BlockChoices(document, context, choices);
		return std::any_of(choices.begin(), choices.end(), [](const EffectGraphEditOps::BlockChoice& choice) {
			return choice.def == GRAPH_EFFECT_CUSTOM_HLSL_BLOCK && choice.category == "HLSL" && choice.name == "Custom HLSL";
		});
	};
	EXPECT_TRUE(offered(1));
	EXPECT_TRUE(offered(2));
	EXPECT_TRUE(offered(3));
	EXPECT_TRUE(EffectGraphEditOps::IsOperator(*GraphNodeLibrary::Find(GRAPH_EFFECT_CUSTOM_HLSL_OPERATOR)));
	EXPECT_TRUE(GraphEffectRules::IsEffectOnly(GRAPH_EFFECT_CUSTOM_HLSL_OPERATOR));

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const str inOutput = Block(52, "void Fade(inout EffectAttributes attributes) { attributes.alpha *= 0.5; }");
	ASSERT_TRUE(Compile(Effect(Block(50, LIFT), {}, Block(51, LIFT), inOutput), effect, diagnostics)) << Said(diagnostics);
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("SltCustomHLSL_"), str::npos);
}

TEST_F(GraphEffectCustomHLSLFixture, KernelHoldsFunctionOnce) {
	const str blocks = Block(50, LIFT, R"("scale":{"value":[1]})") + "," + Block(51, LIFT, R"("scale":{"value":[2]})") + "," +
					   Block(52, "void Other(inout EffectAttributes attributes) { attributes.alpha = 0.5; }");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(blocks), effect, diagnostics)) << Said(diagnostics);
	const str& kernel = effect.systems[0].initializeKernel;
	EXPECT_EQ(Count(kernel, "struct EffectAttributes"), 2u) << kernel;
	EXPECT_EQ(Count(kernel, "#ifndef SLT_EFFECT_ATTRIBUTES\n#define SLT_EFFECT_ATTRIBUTES\nstruct EffectAttributes"), 2u);
	EXPECT_EQ(Count(kernel, "void Lift_"), 1u);
	size_t wrappers = 0;
	size_t calls = 0;
	for (size_t at = kernel.find("SltCustomHLSL_"); at != str::npos; at = kernel.find("SltCustomHLSL_", at + 1)) {
		const bool declared = at >= 5 && kernel.compare(at - 5, 5, "void ") == 0;
		wrappers += declared ? 1 : 0;
		calls += declared ? 0 : 1;
	}
	EXPECT_EQ(wrappers, 2u);
	EXPECT_EQ(calls, 3u);
	EXPECT_EQ(Count(kernel, "void Other_"), 1u);
}

TEST_F(GraphEffectCustomHLSLFixture, ACPUProgramRefusesCustomHLSLByName) {
	const str text = std::format(R"({{"_type":"EffectGraph","version":3,"nextId":100,"nodes":[
{{"id":40,"def":"HLSL/CustomHLSL","settings":{{"HLSL Code":{}}},"in":{{"value":{{"value":[5]}}}}}}],"contexts":[
{{"id":4,"def":"Context/Spawn","blocks":[{{"id":5,"def":"Block/ConstantSpawnRate","in":{{"Rate":{{"from":[40,"out"]}}}}}}]}},
{{"id":1,"def":"Context/Initialize","flow":[[4,"In"]],"settings":{{"Capacity":64}},"blocks":[]}},
{{"id":3,"def":"Context/OutputParticlePrimitive","flow":[[1,"In"]],"blocks":[]}}]}})",
								 Json(GraphCustomHLSL::DEFAULT_OPERATOR_CODE));
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(text, effect, diagnostics));
	EXPECT_NE(Said(diagnostics).find("[node 40] runs custom HLSL, which only a particle context does"), str::npos) << Said(diagnostics);
	EXPECT_FALSE(std::any_of(diagnostics.begin(), diagnostics.end(), [](const GraphDiagnostic& d) { return d.node != 40; })) << Said(diagnostics);
}

TEST_F(GraphEffectCustomHLSLFixture, ShaderFileIncludedWhole) {
	const Guid file = Add("Assets/Hlsl/Shift.hlsl", "/// Hidden\n"
													"float Twice(float x) { return x * 2; }\n"
													"void Shift(inout EffectAttributes attributes, float amount)\n"
													"{\n"
													"  attributes.size = Twice(amount);\n"
													"}\n");
	ASSERT_TRUE(file.IsValid());
	const str named = std::format(R"(,"Shader File":"{}")", file.ToString());
	const GraphDocument document = Parse(Effect(Block(50, "void Ignored(inout EffectAttributes attributes) {}", {}, named)));
	GraphDefinitions definitions;
	const GraphNodeDefinition* definition = definitions.Find(document, document.contexts[0].blocks[0]);
	ASSERT_NE(definition, nullptr);
	ASSERT_EQ(definition->inputs.size(), 1u);
	EXPECT_EQ(definition->inputs[0].name, "amount");
	ASSERT_EQ(definition->outputs.size(), 1u);
	EXPECT_EQ(definition->outputs[0].name, "size");
	Vector<const GraphReadDocument*> read{GetMemoryResource(MemTag::Editor)};
	definitions.Read(read);
	ASSERT_EQ(read.size(), 1u);
	EXPECT_FALSE(read[0]->physicalPath.empty());
	EXPECT_NE(read[0]->contentHash, 0u);

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(Block(50, "void Ignored(inout EffectAttributes attributes) {}", {}, named)), effect, diagnostics)) << Said(diagnostics);
	const str& kernel = effect.systems[0].initializeKernel;
	EXPECT_EQ(kernel.find("Ignored"), str::npos);
	EXPECT_NE(kernel.find("Shift.hlsl\""), str::npos);
	EXPECT_NE(kernel.find("Shift(attributes"), str::npos);

	const str missing = RefusalOf(Block(50, LIFT, {}, R"(,"Shader File":"0123456789abcdef0123456789abcdef")"));
	EXPECT_NE(missing.find("uses shader file"), str::npos) << missing;
}

TEST_F(GraphEffectCustomHLSLFixture, IncludeResolvesNearGraphOrRoot) {
	Add("Assets/Fx/Near.hlsl", "float Near() { return 1; }\n");
	Add("Assets/Shared/Far.hlsl", "float Far() { return 2; }\n");
	const str code = "#include \"Near.hlsl\"\n"
					 "#include \"Assets/Shared/Far.hlsl\"\n"
					 "void F(inout EffectAttributes attributes) { attributes.size = Near() + Far(); }\n";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(Block(50, code)), effect, diagnostics, "project://Assets/Fx/Graph.sleffect")) << Said(diagnostics);
	const str& kernel = effect.systems[0].initializeKernel;
	const str near = FileSystem::Resolve(Path("project://Assets/Fx/Near.hlsl")).ToString();
	const str far = FileSystem::Resolve(Path("project://Assets/Shared/Far.hlsl")).ToString();
	EXPECT_NE(kernel.find("#include \"" + near + "\""), str::npos) << kernel;
	EXPECT_NE(kernel.find("#include \"" + far + "\""), str::npos) << kernel;
	EXPECT_EQ(kernel.find("#include \"Near.hlsl\""), str::npos);

	diagnostics.clear();
	EXPECT_FALSE(Compile(Effect(Block(50, code)), effect, diagnostics, "project://Assets/Elsewhere/Graph.sleffect"));
	EXPECT_NE(Said(diagnostics).find("[node 50] includes 'Near.hlsl' (neither the graph's folder nor the project has it)"), str::npos)
		<< Said(diagnostics);
}

#ifndef SHALLTEAR_SHARED

namespace {

	class GraphEffectCustomHLSLReadbackFixture : public EffectKernelHarness::GraphEffectBlockFixture {
	protected:
		Guid Add(const char* fileName, std::string_view text) {
			FileSystem::WriteTextFile(Path((_root / fileName).generic_string()), str(text));
			return AssetDatabase::EnsureAssetIdentity(Path(str("project://") + fileName));
		}
	};

} // anonymous namespace

TEST_F(GraphEffectCustomHLSLReadbackFixture, CustomBlockWriteReadsBack) {
	const str blocks = Block(50, LIFT, R"("offset":{"value":[1,2,3]},"scale":{"value":[0.25]})") +
					   R"(,{"id":51,"def":"Block/SetAttribute","settings":{"Attribute":"size","Composition":"Multiply"},"in":{"Size":{"value":[3]}}},)" +
					   Block(52, "void Id(inout EffectAttributes attributes)\n{\n  attributes.color = float3(attributes.particleId, 0, 1);\n}") + "," +
					   Block(53, "void Odd(inout EffectAttributes attributes)\n{\n  if (attributes.particleId % 2 == 1)\n    attributes.alpha = 0.25;\n"
								 "  attributes.scaleY = 2;\n}");
	EffectKernelHarness::BlockRun run;
	if (!Begin(run, EffectKernelHarness::EffectWith({}, blocks, {}), "CustomBlock")) {
		GTEST_SKIP();
	}
	Advance(run, 0.0f, 1.0f / 60.0f, {4});
	const Vector<f32> sizes = Values(run, "size");
	ASSERT_EQ(sizes.size(), 4u);
	for (const f32 size : sizes) {
		EXPECT_FLOAT_EQ(size, 1.5f);
	}
	for (u32 axis = 0; axis < 3; ++axis) {
		for (const f32 value : Values(run, "position", axis)) {
			EXPECT_FLOAT_EQ(value, static_cast<f32>(axis + 1)) << axis;
		}
	}
	EXPECT_EQ(Sorted(Values(run, "color", 0)), (Vector<f32>{{0.0f, 1.0f, 2.0f, 3.0f}, GetMemoryResource(MemTag::Graphics)}));
	for (const f32 blue : Values(run, "color", 2)) {
		EXPECT_FLOAT_EQ(blue, 1.0f);
	}
	const Vector<f32> ids = Values(run, "color", 0);
	const Vector<f32> alphas = Values(run, "alpha");
	ASSERT_EQ(alphas.size(), ids.size());
	for (size_t i = 0; i < ids.size(); ++i) {
		EXPECT_FLOAT_EQ(alphas[i], static_cast<u32>(ids[i]) % 2 == 1 ? 0.25f : 1.0f) << "particle " << ids[i];
	}
	for (u32 axis = 0; axis < 3; ++axis) {
		for (const f32 value : Values(run, "scale", axis)) {
			EXPECT_FLOAT_EQ(value, axis == 1 ? 2.0f : 1.0f) << axis;
		}
	}
}

TEST_F(GraphEffectCustomHLSLReadbackFixture, OperatorsReturnAndOutParameterFeedBlock) {
	const str nodes =
		std::format(R"({{"id":40,"def":"HLSL/CustomHLSL","settings":{{"HLSL Code":{}}},"in":{{"a":{{"value":[3]}},"b":{{"value":[1]}}}}}},)"
					R"({{"id":41,"def":"HLSL/CustomHLSL","settings":{{"HLSL Code":{}}},"in":{{"n":{{"from":[42,"particleId"]}}}}}},)"
					R"({{"id":42,"def":"Attribute/GetAttribute","settings":{{"Attribute":"particleId"}}}})",
					Json("float Sum(float a, float b, out float difference)\n{\n  difference = a - b;\n  return a + b;\n}"),
					Json("uint Twice(uint n) { return n * 2; }"));
	const str blocks = R"({"id":50,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[40,"out"]}}},)"
					   R"({"id":51,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[40,"difference"]}}},)"
					   R"({"id":52,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[41,"out"]}}})";
	EffectKernelHarness::BlockRun run;
	if (!Begin(run, EffectKernelHarness::EffectWith(nodes, blocks, {}), "CustomOperator")) {
		GTEST_SKIP();
	}
	Advance(run, 0.0f, 1.0f / 60.0f, {4});
	const Vector<f32> sizes = Values(run, "size");
	ASSERT_EQ(sizes.size(), 4u);
	for (const f32 size : sizes) {
		EXPECT_FLOAT_EQ(size, 4.0f);
	}
	for (const f32 alpha : Values(run, "alpha")) {
		EXPECT_FLOAT_EQ(alpha, 2.0f);
	}
	EXPECT_EQ(Sorted(Values(run, "color", 0)), (Vector<f32>{{0.0f, 2.0f, 4.0f, 6.0f}, GetMemoryResource(MemTag::Graphics)}));
}

TEST_F(GraphEffectCustomHLSLReadbackFixture, TheRandomMacrosDrawPerParticle) {
	const str blocks = Block(50, "void R(inout EffectAttributes attributes)\n{\n  attributes.size = EFFECT_RAND;\n  attributes.alpha = EFFECT_FIXED_RAND(7);\n}") + "," +
					   Block(51, "void S(inout EffectAttributes attributes)\n{\n  attributes.pivotX = EFFECT_RAND;\n  attributes.angleX = EFFECT_FIXED_RAND(7);\n}");
	EffectKernelHarness::BlockRun run;
	if (!Begin(run, EffectKernelHarness::EffectWith({}, blocks, {}), "CustomRandom")) {
		GTEST_SKIP();
	}
	Advance(run, 0.0f, 1.0f / 60.0f, {4}, 12345u);
	const Vector<f32> sizes = Values(run, "size");
	ASSERT_EQ(sizes.size(), 4u);
	for (const f32 size : sizes) {
		EXPECT_GE(size, 0.0f);
		EXPECT_LT(size, 1.0f);
	}
	EXPECT_EQ(Distinct(sizes), 4u);
	const Vector<f32> alphas = Values(run, "alpha");
	EXPECT_EQ(Distinct(alphas), 4u);
	const Vector<f32> pivots = Values(run, "pivot", 0);
	const Vector<f32> angles = Values(run, "angle", 0);
	ASSERT_EQ(pivots.size(), 4u);
	ASSERT_EQ(angles.size(), 4u);
	for (size_t i = 0; i < sizes.size(); ++i) {
		EXPECT_NE(pivots[i], sizes[i]);
		EXPECT_FLOAT_EQ(angles[i], alphas[i]);
	}
}

TEST_F(GraphEffectCustomHLSLReadbackFixture, ShaderFilesFunctionAndIncludedHelperRun) {
	const Guid file = Add("Shift.hlsl", "/// Hidden\n"
										"float Twice(float x) { return x * 2; }\n"
										"void Shift(inout EffectAttributes attributes, float amount)\n"
										"{\n"
										"  attributes.size = Twice(amount);\n"
										"}\n");
	ASSERT_TRUE(file.IsValid());
	Add("Half.hlsl", "float Half(float x) { return x * 0.5; }\n");
	const str blocks = Block(50, "unused", R"("amount":{"value":[0.75]})", std::format(R"(,"Shader File":"{}")", file.ToString())) + "," +
					   Block(51, "#include \"Half.hlsl\"\nvoid H(inout EffectAttributes attributes)\n{\n  attributes.alpha = Half(attributes.size);\n}");
	EffectKernelHarness::BlockRun run;
	if (!Begin(run, EffectKernelHarness::EffectWith({}, blocks, {}), "CustomFile")) {
		GTEST_SKIP();
	}
	Advance(run, 0.0f, 1.0f / 60.0f, {2});
	const Vector<f32> sizes = Values(run, "size");
	ASSERT_EQ(sizes.size(), 2u);
	for (const f32 size : sizes) {
		EXPECT_FLOAT_EQ(size, 1.5f);
	}
	for (const f32 alpha : Values(run, "alpha")) {
		EXPECT_FLOAT_EQ(alpha, 0.75f);
	}
}

#endif
