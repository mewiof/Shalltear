#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "GraphEffect.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <format>

using namespace Shalltear;

namespace {

	struct Effect {
		str initializeSettings = R"("Capacity":64)";
		str initializeInputs;
		str initializeBlocks;
		bool update = true;
		str updateBlocks;
		str output = str(GRAPH_EFFECT_OUTPUT);
		str outputSettings;
		str outputBlocks;
		str properties;
		str nodes;
	};

	str Text(const Effect& effect) {
		const str update = effect.update ? std::format(R"({{"id":8,"def":"Context/Update","x":0,"y":400,"flow":[[4,"In"]],"blocks":[{}]}},)", effect.updateBlocks)
										 : str();
		return std::format(R"({{"_type":"EffectGraph","version":2,"nextId":100,"settings":{{}},"properties":[{}],"nodes":[{}],"contexts":[
{{"id":2,"def":"Context/Spawn","x":0,"y":0,"blocks":[{{"id":3,"def":"Block/ConstantSpawnRate","in":{{"Rate":{{"value":[10]}}}}}}]}},
{{"id":4,"def":"Context/Initialize","x":0,"y":200,"settings":{{{}}},"flow":[[2,"In"]],"in":{{{}}},"blocks":[{}]}},
{}
{{"id":9,"def":"{}","x":0,"y":600,"settings":{{{}}},"flow":[[{},"In"]],"blocks":[{}]}}
],"groups":[],"notes":[]}})",
						   effect.properties, effect.nodes, effect.initializeSettings, effect.initializeInputs, effect.initializeBlocks, update, effect.output,
						   effect.outputSettings, effect.update ? 8 : 4, effect.outputBlocks);
	}

	GraphDocument Read(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error << "\n"
																   << text;
		return document;
	}

	str Said(const Vector<GraphDiagnostic>& diagnostics) {
		str all;
		for (const GraphDiagnostic& diagnostic : diagnostics) {
			all += std::format("[node {} {} {}] {}\n", diagnostic.node, diagnostic.severity == GraphDiagnosticSeverity::Error ? "error" : "warning",
							   diagnostic.key, diagnostic.message);
		}
		return all;
	}

	Vector<GraphDiagnostic> Warnings(const GraphDocument& document) {
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		GraphEffect effect;
		EXPECT_TRUE(GraphEffectTarget::Compile(document, "warnings.sleffect", effect, diagnostics)) << Said(diagnostics);
		EXPECT_FALSE(HasErrorSince(diagnostics)) << Said(diagnostics);
		for (const GraphDiagnostic& diagnostic : diagnostics) {
			EXPECT_FALSE(diagnostic.key.empty()) << diagnostic.message;
		}
		return diagnostics;
	}
	Vector<GraphDiagnostic> Warnings(const Effect& effect) { return Warnings(Read(Text(effect))); }

	const GraphDiagnostic* Find(const Vector<GraphDiagnostic>& diagnostics, u32 node, std::string_view key) {
		for (const GraphDiagnostic& diagnostic : diagnostics) {
			if (diagnostic.node == node && diagnostic.key == key) {
				return &diagnostic;
			}
		}
		return nullptr;
	}

	u32 Count(const Vector<GraphDiagnostic>& diagnostics, std::string_view key) {
		u32 count = 0;
		for (const GraphDiagnostic& diagnostic : diagnostics) {
			count += diagnostic.key == key ? 1u : 0u;
		}
		return count;
	}

	str Json(std::string_view text) {
		str out = "\"";
		for (const char c : text) {
			out += c == '\n' ? str("\\n") : (c == '"' ? str("\\\"") : str(1, c));
		}
		return out + "\"";
	}

	constexpr std::string_view SET_VELOCITY = R"({"id":22,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"value":[0,1,0]}}})";

} // anonymous namespace

TEST(GraphEffectWarningTest, WarningAccompaniesSuccess) {
	Effect effect;
	effect.updateBlocks = R"({"id":20,"def":"Block/CollisionShape","settings":{"Behavior":"None"}})";
	const GraphDocument document = Read(Text(effect));
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	GraphEffect compiled;
	ASSERT_TRUE(GraphEffectTarget::Compile(document, "warnings.sleffect", compiled, diagnostics)) << Said(diagnostics);
	EXPECT_EQ(compiled.systems.size(), 1u);
	ASSERT_EQ(diagnostics.size(), 1u) << Said(diagnostics);
	EXPECT_EQ(diagnostics[0].severity, GraphDiagnosticSeverity::Warning);
	EXPECT_EQ(diagnostics[0].node, 20u);
	EXPECT_EQ(diagnostics[0].key, "collision-does-nothing");
	EXPECT_EQ(diagnostics[0].message, "This block does nothing (its behavior is None, it writes no collision attributes).");
	EXPECT_EQ(CountDiagnostics(diagnostics, GraphDiagnosticSeverity::Error), 0u);

	effect.updateBlocks = R"({"id":20,"def":"Block/CollisionShape","settings":{"Behavior":"None","Collision Attributes":"WriteAlways"}})";
	EXPECT_TRUE(Warnings(effect).empty());
}

TEST(GraphEffectWarningTest, CollisionWarnings) {
	Effect effect;
	effect.updateBlocks = R"({"id":20,"def":"Block/CollisionShape","settings":{"Behavior":"Kill","Collision Attributes":"WritePunctualContactOnly"}})";
	EXPECT_NE(Find(Warnings(effect), 20, "punctual-contacts-need-bounce"), nullptr) << Said(Warnings(effect));
	effect.updateBlocks = R"({"id":20,"def":"Block/CollisionShape","settings":{"Collision Attributes":"WritePunctualContactOnly"}})";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));

	effect.updateBlocks = str(R"({"id":20,"def":"Block/CollisionShape"},)") + str(SET_VELOCITY);
	const Vector<GraphDiagnostic> after = Warnings(effect);
	ASSERT_NE(Find(after, 20, "velocity-after-collision"), nullptr) << Said(after);
	EXPECT_EQ(Find(after, 20, "velocity-after-collision")->message,
			  "A later block in this context writes velocity and overrides this bounce. Write velocity before the "
			  "collision blocks.");
	effect.updateBlocks = str(SET_VELOCITY) + R"(,{"id":20,"def":"Block/CollisionShape"})";
	EXPECT_TRUE(Warnings(effect).empty());
	effect.updateBlocks = R"({"id":20,"def":"Block/CollisionShape"},{"id":21,"def":"Block/CollisionShape","settings":{"Shape":"Sphere"}})";
	EXPECT_TRUE(Warnings(effect).empty());
	effect.updateBlocks = R"({"id":20,"def":"Block/CollisionShape"},{"id":22,"def":"Block/SetAttribute","enabled":false,"settings":{"Attribute":"velocity"}})";
	EXPECT_TRUE(Warnings(effect).empty());
}

TEST(GraphEffectWarningTest, UnfireableTriggerWarns) {
	Effect effect;
	effect.updateBlocks = R"({"id":20,"def":"Block/TriggerEvent","settings":{"Mode":"OnCollide"}})";
	EXPECT_NE(Find(Warnings(effect), 20, "trigger-collide-without-contacts"), nullptr) << Said(Warnings(effect));
	effect.updateBlocks =
		R"({"id":21,"def":"Block/CollisionShape","settings":{"Collision Attributes":"WriteAlways"}},{"id":20,"def":"Block/TriggerEvent","settings":{"Mode":"OnCollide"}})";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
	effect.updateBlocks =
		R"({"id":20,"def":"Block/TriggerEvent","settings":{"Mode":"OnCollide"}},{"id":21,"def":"Block/CollisionShape","settings":{"Collision Attributes":"WriteAlways"}})";
	EXPECT_NE(Find(Warnings(effect), 20, "trigger-collide-without-contacts"), nullptr);
	effect.updateBlocks =
		R"({"id":21,"def":"Block/CollisionShape"},{"id":20,"def":"Block/TriggerEvent","settings":{"Mode":"OnCollide"}})";
	EXPECT_NE(Find(Warnings(effect), 20, "trigger-collide-without-contacts"), nullptr);

	effect.updateBlocks = {};
	effect.initializeBlocks = R"({"id":20,"def":"Block/TriggerEvent","settings":{"Mode":"OverTime"}})";
	EXPECT_NE(Find(Warnings(effect), 20, "trigger-rate-in-initialize"), nullptr) << Said(Warnings(effect));
	effect.initializeBlocks = {};
	effect.updateBlocks = R"({"id":20,"def":"Block/TriggerEvent","settings":{"Mode":"OverDistance"}})";
	EXPECT_TRUE(Warnings(effect).empty());
}

TEST(GraphEffectWarningTest, BadConstantAttributeWarns) {
	Effect effect;
	effect.initializeBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[20000]}}})";
	const Vector<GraphDiagnostic> long_ = Warnings(effect);
	ASSERT_NE(Find(long_, 20, "lifetime-very-long"), nullptr) << Said(long_);
	EXPECT_EQ(Find(long_, 20, "lifetime-very-long")->message,
			  "This lifetime is 05h33m.\nA particle with no Set Lifetime block never dies (if that's the intent).");
	effect.initializeBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}})";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
	effect.nodes = R"({"id":13,"def":"Math/Multiply","x":0,"y":0,"in":{"A":{"value":[10000]},"B":{"value":[3]}}})";
	effect.initializeBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"from":[13,"Out"]}}})";
	EXPECT_NE(Find(Warnings(effect), 20, "lifetime-very-long"), nullptr);
	effect.properties = R"({"id":1,"name":"Life","reference":"life","type":"Float","default":[20000]})";
	effect.nodes = R"({"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}})";
	effect.initializeBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"from":[10,"Out"]}}})";
	EXPECT_TRUE(Warnings(effect).empty());

	effect = {};
	effect.initializeBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"value":[1,0,1]}}})";
	EXPECT_NE(Find(Warnings(effect), 20, "scale-zero"), nullptr) << Said(Warnings(effect));
	effect.initializeBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"scale"},"in":{"Scale":{"value":[1,0.001,1]}}})";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
}

TEST(GraphEffectWarningTest, ACircleWithNoTurnWarns) {
	Effect effect;
	effect.initializeBlocks = R"({"id":20,"def":"Block/PositionSequential","settings":{"Shape":"Circle"},"in":{"Count":{"value":[0]}}})";
	EXPECT_NE(Find(Warnings(effect), 20, "circle-count-zero"), nullptr) << Said(Warnings(effect));
	effect.initializeBlocks = R"({"id":20,"def":"Block/PositionSequential","settings":{"Shape":"Circle"},"in":{"Up":{"value":[0,0,2]}}})";
	EXPECT_NE(Find(Warnings(effect), 20, "circle-axes-parallel"), nullptr) << Said(Warnings(effect));
	effect.initializeBlocks = R"({"id":20,"def":"Block/PositionSequential","settings":{"Shape":"Circle"}})";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
	effect.initializeBlocks = R"({"id":20,"def":"Block/PositionSequential","in":{"Count":{"value":[0]}}})";
	EXPECT_TRUE(Warnings(effect).empty());
}

TEST(GraphEffectWarningTest, DuplicateHLSLFunctionWarns) {
	const str block = "void Push(inout EffectAttributes attributes, in float3 offset)\n{\n  attributes.position += offset;\n}\n"
					  "void Push(inout EffectAttributes attributes, in float speed)\n{\n  attributes.velocity *= speed;\n}";
	Effect effect;
	effect.updateBlocks = std::format(R"({{"id":20,"def":"Block/CustomHLSL","settings":{{"HLSL Code":{}}}}})", Json(block));
	const Vector<GraphDiagnostic> twice = Warnings(effect);
	ASSERT_NE(Find(twice, 20, "hlsl-duplicate-function"), nullptr) << Said(twice);
	EXPECT_EQ(Find(twice, 20, "hlsl-duplicate-function")->message, "More than one function is named 'Push' (only the 1st can be picked)");

	const str operation = "float Twice(in float value)\n{\n  return value * 2;\n}\nfloat Twice(in float value)\n{\n  return value + value;\n}";
	effect.updateBlocks = {};
	effect.nodes = std::format(R"({{"id":40,"def":"HLSL/CustomHLSL","x":0,"y":0,"settings":{{"HLSL Code":{}}}}})", Json(operation));
	EXPECT_NE(Find(Warnings(effect), 40, "hlsl-duplicate-function"), nullptr) << Said(Warnings(effect));
}

TEST(GraphEffectWarningTest, InitializeWarnsOfCapacityAndBounds) {
	Effect effect;
	effect.initializeSettings = R"("Capacity":2000000)";
	EXPECT_NE(Find(Warnings(effect), 4, "capacity-over-a-million"), nullptr) << Said(Warnings(effect));
	effect.initializeSettings = R"("Capacity":1000000)";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));

	effect.initializeSettings = R"("Capacity":64,"Bounds Setting Mode":"Automatic")";
	const Vector<GraphDiagnostic> automatic = Warnings(effect);
	ASSERT_NE(Find(automatic, 4, "automatic-bounds-culling"), nullptr) << Said(automatic);
	EXPECT_EQ(Find(automatic, 4, "automatic-bounds-culling")->message,
			  "Automatic bounds switch this effect's Culling Flags to 'Always simulate and update bounds'.");

	effect.initializeSettings = R"("Capacity":64)";
	effect.properties = R"({"id":1,"name":"Center","reference":"center","type":"Vector3","default":[0,1,0]})";
	effect.nodes = R"({"id":10,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}})";
	effect.initializeInputs = R"("Bounds Center":{"from":[10,"Out"]})";
	EXPECT_NE(Find(Warnings(effect), 4, "recording-overridden"), nullptr) << Said(Warnings(effect));
	effect.initializeSettings = R"("Capacity":64,"Bounds Setting Mode":"Manual")";
	EXPECT_TRUE(Warnings(effect).empty());
}

TEST(GraphEffectWarningTest, OutputWarnsOfBoundsEscape) {
	Effect effect;
	effect.outputBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[0.5]}}})";
	const Vector<GraphDiagnostic> sized = Warnings(effect);
	ASSERT_NE(Find(sized, 9, "recorded-bounds-may-miss"), nullptr) << Said(sized);
	EXPECT_NE(Find(sized, 9, "recorded-bounds-may-miss")->message.find("measured from Position/Size in Update"), str::npos);
	effect.initializeSettings = R"("Capacity":64,"Bounds Setting Mode":"Manual")";
	EXPECT_TRUE(Warnings(effect).empty());

	effect.initializeSettings = R"("Capacity":64)";
	effect.output = str(GRAPH_EFFECT_OUTPUT_MESH);
	effect.outputBlocks = {};
	const Vector<GraphDiagnostic> mesh = Warnings(effect);
	ASSERT_EQ(Count(mesh, "recorded-bounds-may-miss"), 1u) << Said(mesh);
	EXPECT_NE(Find(mesh, 9, "recorded-bounds-may-miss")->message.find("this output's mesh size"), str::npos);
	effect.outputBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"value":[0.5]}}})";
	EXPECT_EQ(Count(Warnings(effect), "recorded-bounds-may-miss"), 2u);
}

TEST(GraphEffectWarningTest, SortedOutputWarnings) {
	Effect effect;
	effect.initializeSettings = R"("Capacity":64,"Bounds Setting Mode":"Manual")";
	effect.outputBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,1,0]}}})";
	EXPECT_NE(Find(Warnings(effect), 9, "sort-before-output"), nullptr) << Said(Warnings(effect));
	effect.outputSettings = R"("Sort":"Off")";
	EXPECT_TRUE(Warnings(effect).empty());
	effect.outputSettings = R"("Sort":"On","Sort Mode":"YoungestInFront")";
	EXPECT_EQ(Find(Warnings(effect), 9, "sort-before-output"), nullptr);

	effect.outputBlocks = {};
	effect.update = false;
	EXPECT_NE(Find(Warnings(effect), 9, "sort-without-age"), nullptr) << Said(Warnings(effect));
	effect.update = true;
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
}

TEST(GraphEffectWarningTest, FlipbookPlayerWarnings) {
	Effect effect;
	effect.initializeSettings = R"("Capacity":64,"Bounds Setting Mode":"Manual")";
	effect.updateBlocks = R"({"id":20,"def":"Block/FlipbookPlayer","settings":{"Mode":"Cycles"}})";
	const Vector<GraphDiagnostic> bare = Warnings(effect);
	EXPECT_NE(Find(bare, 20, "flipbook-cycles-need-lifetime"), nullptr) << Said(bare);
	EXPECT_NE(Find(bare, 20, "flipbook-needs-output"), nullptr) << Said(bare);
	effect.initializeBlocks = R"({"id":21,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"value":[2]}}})";
	effect.outputSettings = R"("UV Mode":"Flipbook")";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
}

TEST(GraphEffectWarningTest, AStripOutputWritingPositionWarns) {
	Effect effect;
	effect.initializeSettings = R"("Data Type":"ParticleStrip","Bounds Setting Mode":"Manual")";
	effect.output = str(GRAPH_EFFECT_OUTPUT_STRIP);
	effect.outputSettings = R"("Sort":"Off")";
	effect.outputBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[0,1,0]}}})";
	EXPECT_NE(Find(Warnings(effect), 9, "strip-output-writes-position"), nullptr) << Said(Warnings(effect));
	effect.outputBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"value":[1,0,0]}}})";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
}

TEST(GraphEffectWarningTest, PerParticleStripIndexWarns) {
	Effect effect;
	effect.initializeSettings = R"("Data Type":"ParticleStrip","Strip Capacity":4,"Bounds Setting Mode":"Manual")";
	effect.output = str(GRAPH_EFFECT_OUTPUT_STRIP);
	effect.outputSettings = R"("Sort":"Off")";
	effect.nodes = R"({"id":10,"def":"Attribute/GetAttribute","x":0,"y":0,"settings":{"Attribute":"particleId"}},{"id":11,"def":"Attribute/RatioOverStrip","x":0,"y":0})";
	effect.initializeInputs = R"("Strip Index":{"from":[10,"particleId"]})";
	effect.initializeBlocks = R"({"id":20,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[11,"t"]}}})";
	EXPECT_NE(Find(Warnings(effect), 4, "strip-count-needs-constant-index"), nullptr) << Said(Warnings(effect));
	effect.initializeInputs = R"("Strip Index":{"value":[2]})";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
	effect.properties = R"({"id":1,"name":"Lane","reference":"lane","type":"Float","default":[1]})";
	effect.nodes += R"(,{"id":12,"def":"Input/Property","x":0,"y":0,"settings":{"property":1}})";
	effect.initializeInputs = R"("Strip Index":{"from":[12,"Out"]})";
	EXPECT_TRUE(Warnings(effect).empty()) << Said(Warnings(effect));
	effect.initializeInputs = R"("Strip Index":{"from":[10,"particleId"]})";
	effect.initializeBlocks = {};
	EXPECT_TRUE(Warnings(effect).empty());
}

TEST(GraphEffectWarningTest, IgnoredWarningHiddenUntilCleared) {
	Effect effect;
	effect.updateBlocks = R"({"id":20,"def":"Block/CollisionShape","settings":{"Behavior":"None"}})";
	GraphDocument document = Read(Text(effect));
	ASSERT_EQ(Warnings(document).size(), 1u);

	EXPECT_FALSE(EffectGraphEditOps::IgnoreError(document, 20, ""));
	ASSERT_TRUE(EffectGraphEditOps::IgnoreError(document, 20, "collision-does-nothing"));
	EXPECT_FALSE(EffectGraphEditOps::IgnoreError(document, 20, "collision-does-nothing"));
	EXPECT_TRUE(Warnings(document).empty()) << Said(Warnings(document));
	ASSERT_TRUE(EffectGraphEditOps::IgnoreError(document, 20, "velocity-after-collision"));
	const GraphNode* block = GraphDocumentOps::FindNode(document, 20);
	ASSERT_NE(block, nullptr);
	EXPECT_TRUE(GraphEffectRules::IgnoresError(*block, "collision-does-nothing"));

	GraphDocument reread = Read(GraphDocumentOps::Write(document));
	EXPECT_TRUE(Warnings(reread).empty());

	ASSERT_TRUE(EffectGraphEditOps::ClearIgnoredErrors(reread, 20));
	EXPECT_FALSE(EffectGraphEditOps::ClearIgnoredErrors(reread, 20));
	EXPECT_NE(Find(Warnings(reread), 20, "collision-does-nothing"), nullptr);
	EXPECT_FALSE(GraphEffectRules::HasIgnoredErrors(*GraphDocumentOps::FindNode(reread, 20)));
}

TEST(GraphEffectWarningTest, TemplatesWarnOnlyOnRecordedBounds) {
	ASSERT_GT(EffectGraphEditOps::TemplateCount(), 1u);
	bool simpleLoop = false;
	for (u32 i = 0; i < EffectGraphEditOps::TemplateCount(); ++i) {
		const GraphDocument document = Read(EffectGraphEditOps::TemplateDocument(i));
		const Vector<GraphDiagnostic> said = Warnings(document);
		for (const GraphDiagnostic& diagnostic : said) {
			const GraphContext* context = nullptr;
			for (const GraphContext& held : document.contexts) {
				context = held.node.id == diagnostic.node ? &held : context;
			}
			EXPECT_EQ(diagnostic.key, "recorded-bounds-may-miss") << EffectGraphEditOps::TemplateLabel(i) << ": " << Said(said);
			EXPECT_TRUE(context && GraphEffectRules::IsOutput(context->node.def)) << EffectGraphEditOps::TemplateLabel(i) << ": " << Said(said);
		}
		if (EffectGraphEditOps::TemplateLabel(i).ends_with("/Simple Loop")) {
			simpleLoop = true;
			EXPECT_EQ(said.size(), 1u) << Said(said);
		}
	}
	EXPECT_TRUE(simpleLoop);
}
