#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <format>

#ifndef SHALLTEAR_SHARED
#include "EffectKernelHarness.h"
#endif

using namespace Shalltear;

namespace {

	namespace Ops = EffectGraphEditOps;

	GraphDocument ReadGraph(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	str StripEffect(u32 strips, u32 perStrip, std::string_view nodes, std::string_view initialize, std::string_view update = "",
					std::string_view stripIndex = "", std::string_view output = GRAPH_EFFECT_OUTPUT_STRIP, std::string_view outputBlocks = "",
					std::string_view outputSettings = "{}") {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"nodes":[{}],"contexts":[
			{{"id":1,"def":"Context/Initialize","settings":{{"Data Type":"ParticleStrip","Strip Capacity":{},"Particle Per Strip Count":{}}},
				"in":{{{}}},"blocks":[{}]}},
			{{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{}]}},
			{{"id":3,"def":"{}","flow":[[2,"In"]],"settings":{},"blocks":[{}]}}]}})",
						   nodes, strips, perStrip, stripIndex.empty() ? str() : std::format(R"("Strip Index":{})", stripIndex), initialize, update, output,
						   outputSettings, outputBlocks);
	}

	bool Compiles(std::string_view text, GraphEffect& effect, Vector<GraphDiagnostic>& diagnostics) {
		diagnostics.clear();
		return GraphEffectTarget::Compile(ReadGraph(text), "strip.sleffect", effect, diagnostics);
	}

	bool Said(const Vector<GraphDiagnostic>& diagnostics, u32 node, std::string_view what) {
		return std::any_of(diagnostics.begin(), diagnostics.end(),
						   [&](const GraphDiagnostic& diagnostic) { return diagnostic.node == node && diagnostic.message.find(what) != str::npos; });
	}

	str First(const Vector<GraphDiagnostic>& diagnostics) { return diagnostics.empty() ? str() : diagnostics[0].message; }

} // anonymous namespace

TEST(GraphEffectStripTest, StripSystemsCapacityIsStripsTimesParticles) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compiles(StripEffect(4, 16, "", ""), effect, diagnostics)) << First(diagnostics);
	EXPECT_EQ(effect.systems[0].strips, 4u);
	EXPECT_EQ(effect.systems[0].capacity, 64u);
	ASSERT_EQ(effect.systems[0].outputs.size(), 1u);
	EXPECT_TRUE(effect.systems[0].outputs[0].strip);
	EXPECT_EQ(effect.systems[0].outputs[0].vertices, 6u);

	str particles = StripEffect(1, 8, "", "", "", "", GRAPH_EFFECT_OUTPUT);
	particles.replace(particles.find("\"Data Type\":\"ParticleStrip\""), std::string_view("\"Data Type\":\"ParticleStrip\"").size(), "\"Capacity\":12");
	ASSERT_TRUE(Compiles(particles, effect, diagnostics)) << First(diagnostics);
	EXPECT_EQ(effect.systems[0].strips, 0u);
	EXPECT_EQ(effect.systems[0].capacity, 12u);
	EXPECT_FALSE(effect.systems[0].outputs[0].strip);

	EXPECT_FALSE(Compiles(StripEffect(0, 8, "", ""), effect, diagnostics));
	EXPECT_TRUE(Said(diagnostics, 1, "Strip Capacity")) << First(diagnostics);
	EXPECT_FALSE(Compiles(StripEffect(2, 9000000, "", ""), effect, diagnostics));
	EXPECT_TRUE(Said(diagnostics, 1, "past the")) << First(diagnostics);
	str half = StripEffect(1, 8, "", "");
	half.replace(half.find("\"Particle Per Strip Count\":8"), std::string_view("\"Particle Per Strip Count\":8").size(), "\"Particle Per Strip Count\":7.5");
	EXPECT_FALSE(Compiles(half, effect, diagnostics));
	EXPECT_TRUE(Said(diagnostics, 1, "Particle Per Strip Count")) << First(diagnostics);
}

TEST(GraphEffectStripTest, StripOutputDrawsStripSystemAlone) {
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	str particles = StripEffect(1, 8, "", "");
	particles.replace(particles.find("\"Data Type\":\"ParticleStrip\""), std::string_view("\"Data Type\":\"ParticleStrip\"").size(), "\"Data Type\":\"Particle\"");
	EXPECT_FALSE(Compiles(particles, effect, diagnostics));
	EXPECT_TRUE(Said(diagnostics, 3, "Particle Strip")) << First(diagnostics);
	EXPECT_TRUE(Compiles(StripEffect(1, 8, "", "", "", "", GRAPH_EFFECT_OUTPUT), effect, diagnostics)) << First(diagnostics);
	EXPECT_TRUE(Compiles(StripEffect(1, 8, "", "", "", "", GRAPH_EFFECT_OUTPUT_POINT), effect, diagnostics)) << First(diagnostics);

	GraphDocument document;
	document.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
	const u32 particleInit = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	const u32 particleUpdate = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	ASSERT_TRUE(Ops::Link(document, particleInit, particleUpdate, GRAPH_EFFECT_IN));
	Vector<Ops::BlockChoice> contexts{GetMemoryResource(MemTag::Editor)};
	Ops::ContextChoices(contexts);
	const auto strips = std::find_if(contexts.begin(), contexts.end(), [](const Ops::BlockChoice& choice) { return choice.name == "Initialize Particle Strip"; });
	ASSERT_NE(strips, contexts.end());
	const u32 stripInit = Ops::AddContext(document, *strips, Vec2());
	const u32 stripUpdate = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	ASSERT_TRUE(Ops::Link(document, stripInit, stripUpdate, GRAPH_EFFECT_IN));
	for (std::string_view def : {GRAPH_EFFECT_OUTPUT_STRIP, GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP}) {
		SCOPED_TRACE(def);
		const u32 output = Ops::AddContext(document, def, Vec2());
		str why;
		EXPECT_FALSE(Ops::CanLink(document, particleInit, output, GRAPH_EFFECT_IN, &why));
		EXPECT_NE(why.find("strips"), str::npos) << why;
		EXPECT_FALSE(Ops::CanLink(document, particleUpdate, output, GRAPH_EFFECT_IN));
		EXPECT_TRUE(Ops::CanLink(document, stripInit, output, GRAPH_EFFECT_IN));
		EXPECT_TRUE(Ops::CanLink(document, stripUpdate, output, GRAPH_EFFECT_IN));
	}
	const u32 quad = Ops::AddContext(document, GRAPH_EFFECT_OUTPUT, Vec2());
	EXPECT_TRUE(Ops::CanLink(document, stripUpdate, quad, GRAPH_EFFECT_IN));
}

TEST(GraphEffectStripTest, StripSetIsReadWhereProgramRuns) {
	const str nodes = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleIndexInStrip"}},
		{"id":21,"def":"Attribute/GetAttribute","settings":{"Attribute":"stripIndex"}},
		{"id":22,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndexInStrip"}},
		{"id":23,"def":"Attribute/RatioOverStrip"})";
	const str sized = R"({"id":10,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[20,"particleIndexInStrip"]}}})";
	const str alpha = R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"alpha"},"in":{"Alpha":{"from":[22,"spawnIndexInStrip"]}}})";
	const str faded = R"({"id":12,"def":"Block/SetAttribute","settings":{"Attribute":"alpha","Composition":"Multiply"},"in":{"Alpha":{"from":[23,"t"]}}})";
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compiles(StripEffect(2, 4, nodes, sized, alpha, "", GRAPH_EFFECT_OUTPUT_STRIP, faded), effect, diagnostics)) << First(diagnostics);
	const GraphEffectSystem& system = effect.systems[0];
	EXPECT_NE(system.initializeKernel.find("SltParticleStripSpawn(sltStrip, sltIndex, sltStripPlace)"), str::npos);
	EXPECT_EQ(system.initializeKernel.find("SltParticlePopDead"), str::npos);
	EXPECT_NE(system.initializeKernel.find("float(sltStripPlace)"), str::npos);
	EXPECT_NE(system.updateKernel.find("SltParticleStripLive(sltIndex)"), str::npos);
	EXPECT_EQ(system.updateKernel.find("SltParticleKill"), str::npos);
	EXPECT_NE(system.outputs[0].shader.find("float(SltParticleOutputStripPlace(sltIndex))"), str::npos);
	const bool kept = std::any_of(system.stored.begin(), system.stored.end(), [](const GraphEffectStored& stored) { return stored.attribute == "spawnIndexInStrip"; });
	EXPECT_TRUE(kept);

	const size_t stored = system.updateKernel.find("SltParticleStore(");
	const size_t tested = system.updateKernel.find("SltParticleStripLive");
	EXPECT_LT(stored, tested);

	str particles = StripEffect(1, 8, nodes, sized, alpha, "", GRAPH_EFFECT_OUTPUT, faded);
	particles.replace(particles.find("\"Data Type\":\"ParticleStrip\""), std::string_view("\"Data Type\":\"ParticleStrip\"").size(), "\"Capacity\":8");
	ASSERT_TRUE(Compiles(particles, effect, diagnostics)) << First(diagnostics);
	EXPECT_EQ(effect.systems[0].initializeKernel.find("SltParticleStrip"), str::npos);
	EXPECT_EQ(effect.systems[0].outputs[0].shader.find("SltParticleOutputStrip"), str::npos);
	EXPECT_NE(effect.systems[0].initializeKernel.find("sltAttr_particleIndexInStrip = 0.0"), str::npos);
}

TEST(GraphEffectStripTest, StripIndexReadsWhatSpawnBeginsWith) {
	const str source = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"stripIndex","Location":"Source"}})";
	const str spawn = R"({"id":4,"def":"Context/Spawn","blocks":[{"id":5,"def":"Block/IncrementStripIndexOnStart","in":{"Strip Max Count":{"value":[3]}}}]},)";
	str text = StripEffect(4, 8, source, "", "", R"({"from":[20,"stripIndex"]})");
	text.replace(text.find(R"("contexts":[)"), std::string_view(R"("contexts":[)").size(), R"("contexts":[)" + spawn);
	text.replace(text.find(R"("id":1,"def":"Context/Initialize",)"), std::string_view(R"("id":1,"def":"Context/Initialize",)").size(),
				 R"("id":1,"def":"Context/Initialize","flow":[[4,"In"]],)");
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compiles(text, effect, diagnostics)) << First(diagnostics);
	const GraphEffectSystem& system = effect.systems[0];
	const bool carried = std::any_of(system.source.begin(), system.source.end(), [](const GraphEffectField& field) { return field.name == "stripIndex"; });
	EXPECT_TRUE(carried);
	ASSERT_EQ(effect.spawners.size(), 1u);
	ASSERT_EQ(effect.spawners[0].blocks.size(), 1u);
	EXPECT_EQ(effect.spawners[0].blocks[0].kind, GraphEffectSpawnBlock::Kind::IncrementStripIndex);
	EXPECT_EQ(effect.spawners[0].blocks[0].attribute, "stripIndex");
	EXPECT_EQ(effect.spawners[0].blocks[0].components, 1u);
	EXPECT_NE(effect.spawners[0].blocks[0].assembly.find("root"), str::npos);
	const str& kernel = system.initializeKernel;
	EXPECT_LT(kernel.find("sltStrip = uint(max("), kernel.find("SltParticleStripSpawn"));

	const str current = R"({"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleIndexInStrip"}})";
	EXPECT_FALSE(Compiles(StripEffect(4, 8, current, "", "", R"({"from":[20,"particleIndexInStrip"]})"), effect, diagnostics));
	EXPECT_TRUE(Said(diagnostics, 20, "strip's attributes")) << First(diagnostics);

	const str rated = R"({"_type":"EffectGraph","version":4,"nextId":100,"nodes":[
		{"id":20,"def":"Attribute/GetAttribute","settings":{"Attribute":"age"}}],"contexts":[
		{"id":4,"def":"Context/Spawn","blocks":[{"id":5,"def":"Block/ConstantSpawnRate","in":{"Rate":{"from":[20,"age"]}}}]},
		{"id":1,"def":"Context/Initialize","flow":[[4,"In"]],"blocks":[]}]})";
	EXPECT_FALSE(Compiles(rated, effect, diagnostics));
	EXPECT_TRUE(Said(diagnostics, 20, "only a particle context reads a particle's attributes")) << First(diagnostics);
}

TEST(GraphEffectStripTest, StripModesOnlyForStrips) {
	const auto orient = [](std::string_view mode) { return std::format(R"({{"id":10,"def":"Block/Orient","settings":{{"Mode":"{}"}}}})", mode); };
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	for (std::string_view mode : {"FaceCameraPosition", "LookAtPosition", "LookAtLine", "Advanced", "CustomZ", "CustomY"}) {
		SCOPED_TRACE(mode);
		EXPECT_TRUE(Compiles(StripEffect(1, 8, "", "", "", "", GRAPH_EFFECT_OUTPUT_STRIP, orient(mode)), effect, diagnostics)) << First(diagnostics);
	}
	for (std::string_view mode : {"FaceCameraPlane", "FixedAxis", "AlongVelocity"}) {
		SCOPED_TRACE(mode);
		EXPECT_FALSE(Compiles(StripEffect(1, 8, "", "", "", "", GRAPH_EFFECT_OUTPUT_STRIP, orient(mode)), effect, diagnostics));
		EXPECT_TRUE(Said(diagnostics, 10, "a strip output doesn't support")) << First(diagnostics);
	}
	for (std::string_view mode : {"CustomZ", "CustomY"}) {
		SCOPED_TRACE(mode);
		EXPECT_FALSE(Compiles(StripEffect(1, 8, "", "", "", "", GRAPH_EFFECT_OUTPUT, orient(mode)), effect, diagnostics));
		EXPECT_TRUE(Said(diagnostics, 10, "only a strip output supports")) << First(diagnostics);
	}
	const str placed = R"({"id":11,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[1,2,3]}}})";
	ASSERT_TRUE(Compiles(StripEffect(1, 8, "", placed, "", "", GRAPH_EFFECT_OUTPUT_STRIP, orient("FaceCameraPosition")), effect, diagnostics))
		<< First(diagnostics);
	const str& shader = effect.systems[0].outputs[0].shader;
	EXPECT_NE(shader.find("float4 sltStripPrevious = SltParticleOutputStripNeighbor(sltIndex, false,"), str::npos);
	const bool kept = std::any_of(effect.systems[0].stored.begin(), effect.systems[0].stored.end(),
								  [](const GraphEffectStored& stored) { return stored.attribute == "position"; });
	EXPECT_TRUE(kept);
	EXPECT_EQ(shader.find("SLT_PARTICLE_CULLED;\n\t\treturn sltOut;"), str::npos);
}

TEST(GraphEffectStripTest, StripOutputsPortsFollowTilingAndTexture) {
	const auto shown = [](std::string_view def, std::string_view settings, std::string_view name, bool port) {
		GraphDocument document = ReadGraph(StripEffect(1, 8, "", "", "", "", def, "", settings));
		const GraphNode& node = document.contexts[2].node;
		const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
		if (port) {
			const i32 at = GraphNodeLibrary::FindInput(*definition, name);
			return at >= 0 && GraphNodeLibrary::InputActive(*definition, static_cast<u32>(at), node);
		}
		for (u32 i = 0; i < definition->settings.size(); ++i) {
			if (definition->settings[i].key == name) {
				return GraphNodeLibrary::SettingActive(definition->settings, i, node);
			}
		}
		return false;
	};
	EXPECT_EQ(GraphNodeLibrary::Find(GRAPH_EFFECT_OUTPUT_STRIP)->name, "Output ParticleStrip Quad");
	EXPECT_EQ(GraphNodeLibrary::Find(GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP)->name, "Output ParticleStrip NodeShader Quad");
	EXPECT_FALSE(shown(GRAPH_EFFECT_OUTPUT_STRIP, "{}", "Tex Coord", true));
	EXPECT_TRUE(shown(GRAPH_EFFECT_OUTPUT_STRIP, R"({"Tiling Mode":"Custom"})", "Tex Coord", true));
	EXPECT_TRUE(shown(GRAPH_EFFECT_OUTPUT_STRIP, "{}", "Swap UV", false));
	EXPECT_FALSE(shown(GRAPH_EFFECT_OUTPUT_STRIP, R"({"Use Base Color Map":"None"})", "Swap UV", false));
	EXPECT_FALSE(shown(GRAPH_EFFECT_OUTPUT_STRIP, "{}", "Primitive Type", false));
	EXPECT_TRUE(shown(GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP, "{}", "Swap UV", false));
	EXPECT_TRUE(shown(GRAPH_EFFECT_OUTPUT_NODE_SHADER_STRIP, R"({"Tiling Mode":"Custom"})", "Tex Coord", true));

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compiles(StripEffect(1, 8, "", "", "", "", GRAPH_EFFECT_OUTPUT_STRIP, "", "{}"), effect, diagnostics)) << First(diagnostics);
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("float2 sltUV = float2(float(SltParticleOutputStripPlace(sltIndex)) / "), str::npos);
	ASSERT_TRUE(Compiles(StripEffect(1, 8, "", "", "", "", GRAPH_EFFECT_OUTPUT_STRIP, "", R"({"Tiling Mode":"RepeatPerSegment","Swap UV":true})"), effect, diagnostics))
		<< First(diagnostics);
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("float2 sltUV = float2(float(sltEnd), "), str::npos);
	EXPECT_NE(effect.systems[0].outputs[0].shader.find("sltUV = float2(1.0 - sltUV.y, sltUV.x);"), str::npos);
}

TEST(GraphEffectStripTest, TheSearcherOffersStripsInTheirCategory) {
	Vector<Ops::BlockChoice> contexts{GetMemoryResource(MemTag::Editor)};
	Ops::ContextChoices(contexts);
	Vector<str> names{GetMemoryResource(MemTag::Editor)};
	for (const Ops::BlockChoice& choice : contexts) {
		names.push_back(choice.name);
	}
	const auto at = [&](std::string_view name) { return std::find(names.begin(), names.end(), name) - names.begin(); };
	ASSERT_LT(at("Initialize Particle Strip"), static_cast<std::ptrdiff_t>(names.size()));
	EXPECT_EQ(at("Initialize Particle Strip"), at("Initialize Particle") + 1);
	EXPECT_LT(at("Output ParticleStrip Quad"), static_cast<std::ptrdiff_t>(names.size()));
	EXPECT_LT(at("Output ParticleStrip NodeShader Quad"), static_cast<std::ptrdiff_t>(names.size()));
	GraphDocument document;
	document.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
	const u32 made = Ops::AddContext(document, contexts[static_cast<size_t>(at("Initialize Particle Strip"))], Vec2());
	ASSERT_NE(made, 0u);
	EXPECT_TRUE(GraphEffectRules::MakesStrips(document.contexts.back().node));
	GraphDefinitions definitions;
	const GraphNodeDefinition* built = definitions.Find(document, document.contexts.back().node);
	ASSERT_NE(built, nullptr);
	EXPECT_EQ(built->name, "Initialize Particle Strip");

	const auto offered = [&](std::string_view context, std::string_view name) {
		GraphDocument holder;
		holder.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
		const u32 id = Ops::AddContext(holder, context, Vec2());
		Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		Ops::BlockChoices(holder, id, choices);
		return std::any_of(choices.begin(), choices.end(), [&](const Ops::BlockChoice& choice) { return choice.name == name; });
	};
	EXPECT_TRUE(offered(GRAPH_EFFECT_SPAWN, "Increment Strip Index On Start"));
	EXPECT_FALSE(offered(GRAPH_EFFECT_INITIALIZE, "Increment Strip Index On Start"));
	EXPECT_TRUE(offered(GRAPH_EFFECT_OUTPUT_STRIP, "Orient: Custom Z"));
	EXPECT_TRUE(offered(GRAPH_EFFECT_OUTPUT_STRIP, "Orient: Custom Y"));

	Vector<Ops::BlockChoice> operators{GetMemoryResource(MemTag::Editor)};
	Ops::OperatorChoices(operators);
	for (std::string_view attribute : {"stripIndex", "particleIndexInStrip", "particleCountInStrip", "spawnIndexInStrip"}) {
		const bool listed = std::any_of(operators.begin(), operators.end(),
										[&](const Ops::BlockChoice& choice) { return choice.name == std::format("Get Attribute: {}", attribute); });
		EXPECT_TRUE(listed) << attribute;
	}
	EXPECT_TRUE(Ops::IsOperator(*GraphNodeLibrary::Find(GRAPH_EFFECT_RATIO_OVER_STRIP)));
	EXPECT_EQ(GraphNodeLibrary::Find(GRAPH_EFFECT_RATIO_OVER_STRIP)->name, "Get Ratio Over Strip [0..1]");
}

#ifndef SHALLTEAR_SHARED

using namespace Shalltear::EffectKernelHarness;

namespace {

	constexpr std::string_view STRIP_SET_NODES = R"({"id":30,"def":"Attribute/GetAttribute","settings":{"Attribute":"stripIndex"}},
		{"id":31,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleIndexInStrip"}},
		{"id":32,"def":"Attribute/GetAttribute","settings":{"Attribute":"spawnIndexInStrip"}},
		{"id":33,"def":"Attribute/GetAttribute","settings":{"Attribute":"particleCountInStrip"}},
		{"id":34,"def":"Input/Vector3","in":{"X":{"from":[30,"stripIndex"]},"Y":{"from":[31,"particleIndexInStrip"]},"Z":{"from":[32,"spawnIndexInStrip"]}}})";
	constexpr std::string_view STRIP_SET_BLOCKS = R"({"id":40,"def":"Block/SetAttribute","settings":{"Attribute":"color"},"in":{"Color":{"from":[34,"Out"]}}},
		{"id":41,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[33,"particleCountInStrip"]}}})";

	str TakingTurns(u32 strips) {
		return std::format(R"({{"id":50,"def":"Attribute/GetAttribute","settings":{{"Attribute":"spawnIndex"}}}},
			{{"id":51,"def":"Math/Modulo","in":{{"A":{{"from":[50,"spawnIndex"]}},"B":{{"value":[{},0,0,0]}}}}}})",
						   strips);
	}

	str LifetimeBlock() {
		return R"({"id":42,"def":"Block/SetAttribute","settings":{"Attribute":"lifetime"},"in":{"Lifetime":{"from":[69,"Out"]}}})";
	}
	str LifetimeNodes(std::string_view expression) {
		return std::format(R"({},{{"id":68,"def":"Math/Multiply","in":{{"A":{{"from":[60,"Out"]}},"B":{{"value":[10,0,0,0]}}}}}},
			{{"id":69,"def":"Math/Add","in":{{"A":{{"from":[68,"Out"]}},"B":{{"value":[0.05,0,0,0]}}}}}})",
						   expression);
	}

	struct Particle {
		u32 strip = 0;
		u32 place = 0;
		u32 spawnInStrip = 0;
		u32 count = 0;
	};

} // anonymous namespace

class ParticleStripFixture : public GraphEffectBlockFixture {
protected:
	static constexpr f32 DELTA = 0.1f;

	static Vector<Particle> Particles(const BlockRun& run) {
		const Vector<f32> strip = Values(run, "color", 0);
		const Vector<f32> place = Values(run, "color", 1);
		const Vector<f32> spawned = Values(run, "color", 2);
		const Vector<f32> count = Values(run, "size", 0);
		Vector<Particle> out{GetMemoryResource(MemTag::Graphics)};
		for (size_t i = 0; i < strip.size(); ++i) {
			out.push_back({static_cast<u32>(strip[i]), static_cast<u32>(place[i]), static_cast<u32>(spawned[i]), static_cast<u32>(count[i])});
		}
		return out;
	}

	static u32 AliveCount(const BlockRun& run) {
		Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
		return GraphEffectKernelFixture::Alive(run.system, indices);
	}
};

TEST_F(ParticleStripFixture, SpawnFillsStripInOrder) {
	BlockRun run;
	if (!Begin(run, StripEffect(2, 4, str(STRIP_SET_NODES) + "," + TakingTurns(2), STRIP_SET_BLOCKS, "", R"({"from":[51,"Out"]})"), "StripTurns")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(run, 0.0f, DELTA, {6});
	Vector<Particle> first = Particles(run);
	ASSERT_EQ(first.size(), 6u);
	for (u32 strip = 0; strip < 2; ++strip) {
		Vector<u32> places{GetMemoryResource(MemTag::Graphics)};
		for (const Particle& particle : first) {
			if (particle.strip == strip) {
				places.push_back(particle.place);
				EXPECT_EQ(particle.spawnInStrip, particle.place);
				EXPECT_EQ(particle.count, 6u);
			}
		}
		std::sort(places.begin(), places.end());
		EXPECT_EQ(places, (Vector<u32>{{0, 1, 2}, GetMemoryResource(MemTag::Graphics)})) << "strip " << strip;
		const StripWords words = Strip(run.system, strip);
		EXPECT_EQ(words.first, 0u);
		EXPECT_EQ(words.count, 3u);
		EXPECT_EQ(words.began, 3u);
	}

	Advance(run, DELTA, DELTA, {4});
	EXPECT_EQ(AliveCount(run), 8u);
	EXPECT_EQ(Dropped(run.system), 2u);
	u32 secondFrame = 0;
	for (const Particle& particle : Particles(run)) {
		if (particle.place == 3) {
			++secondFrame;
			EXPECT_EQ(particle.spawnInStrip, 0u);
			EXPECT_EQ(particle.count, 7u);
		}
	}
	EXPECT_EQ(secondFrame, 2u);
	for (u32 strip = 0; strip < 2; ++strip) {
		const StripWords words = Strip(run.system, strip);
		EXPECT_EQ(words.count, 4u) << "strip " << strip;
		EXPECT_EQ(words.first, 0u);
	}
}

TEST_F(ParticleStripFixture, StripIndexPastStripsIsDropped) {
	BlockRun past;
	if (!Begin(past, StripEffect(2, 4, STRIP_SET_NODES, STRIP_SET_BLOCKS, "", R"({"value":[2]})"), "StripPast")) {
		GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
	}
	Advance(past, 0.0f, DELTA, {3});
	EXPECT_EQ(AliveCount(past), 0u);
	EXPECT_EQ(Dropped(past.system), 3u);

	BlockRun below;
	ASSERT_TRUE(Begin(below, StripEffect(2, 4, STRIP_SET_NODES, STRIP_SET_BLOCKS, "", R"({"value":[-1]})"), "StripBelow"));
	Advance(below, 0.0f, DELTA, {2});
	EXPECT_EQ(AliveCount(below), 2u);
	EXPECT_EQ(Strip(below.system, 0).count, 2u);
}

TEST_F(ParticleStripFixture, StripDropsDeadEnds) {
	struct Case {
		const char* name;
		str expression;
		u32 alive;
		u32 first;
		u32 count;
	};
	const Case cases[] = {
		{"Middle",
		 R"({"id":61,"def":"Math/Subtract","in":{"A":{"from":[31,"particleIndexInStrip"]},"B":{"value":[2,0,0,0]}}},
			{"id":62,"def":"Math/Sign","in":{"In":{"from":[61,"Out"]}}},
			{"id":60,"def":"Math/Absolute","in":{"In":{"from":[62,"Out"]}}})",
		 4, 0, 5},
		{"Head", R"({"id":60,"def":"Math/Step","in":{"Edge":{"value":[2,0,0,0]},"In":{"from":[31,"particleIndexInStrip"]}}})", 3, 2, 3},
		{"Tail", R"({"id":60,"def":"Math/Step","in":{"Edge":{"from":[31,"particleIndexInStrip"]},"In":{"value":[3,0,0,0]}}})", 4, 0, 4},
		{"HeadThroughMiddle", R"({"id":60,"def":"Math/Step","in":{"Edge":{"value":[3,0,0,0]},"In":{"from":[31,"particleIndexInStrip"]}}})", 2, 3, 2},
	};
	for (const Case& entry : cases) {
		SCOPED_TRACE(entry.name);
		BlockRun run;
		const str nodes = str(STRIP_SET_NODES) + "," + LifetimeNodes(entry.expression);
		if (!Begin(run, StripEffect(1, 8, nodes, str(STRIP_SET_BLOCKS) + "," + LifetimeBlock()), (str("StripDeath") + entry.name).c_str())) {
			GTEST_SKIP() << "dxcompiler.dll unavailable and shader cache cold";
		}
		Advance(run, 0.0f, DELTA, {5});
		const StripWords born = Strip(run.system, 0);
		EXPECT_EQ(born.count, 5u);
		Advance(run, DELTA, DELTA, {});
		EXPECT_EQ(AliveCount(run), entry.alive);
		const StripWords words = Strip(run.system, 0);
		EXPECT_EQ(words.first, entry.first);
		EXPECT_EQ(words.count, entry.count);
		EXPECT_EQ(words.began, entry.count);
		EXPECT_EQ(words.least, 0xFFFFFFFFu);
	}

	BlockRun middle;
	ASSERT_TRUE(Begin(middle, StripEffect(1, 8, str(STRIP_SET_NODES) + "," + LifetimeNodes(cases[0].expression), str(STRIP_SET_BLOCKS) + "," + LifetimeBlock()),
					  "StripDeathKept"));
	Advance(middle, 0.0f, DELTA, {5});
	Advance(middle, DELTA, DELTA, {});
	u32 colorSlot = 0;
	for (const GraphEffectStored& stored : middle.effect.systems[0].stored) {
		colorSlot = stored.attribute == "color" ? stored.slot : colorSlot;
	}
	const StripWords words = Strip(middle.system, 0);
	const u32 third = (words.first + 2) % 8;
	EXPECT_EQ(Attribute(middle.system, colorSlot + 1, third), 2.0f);
	Advance(middle, 2 * DELTA, DELTA, {});
	EXPECT_EQ(Strip(middle.system, 0).count, 5u);
}

#endif
