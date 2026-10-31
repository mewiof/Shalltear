#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "EffectSubgraphImporter.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphNodeLibrary.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
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

	class EffectGraphStandIn final : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("VisualEffectAsset"); }
		Asset* Import(const Path&) override { return nullptr; }
	};

	str Burst(std::string_view extra = {}, std::string_view spawn = {}, std::string_view nodes = {}) {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,
"properties":[{{"id":1,"name":"Size","reference":"Size","type":"Float","default":[0.5]}},{{"id":2,"name":"Tint","reference":"Tint","type":"Color","default":[1,0,0,1]}}],
"nodes":[{{"id":10,"def":"Input/Property","x":0,"y":0,"settings":{{"property":1}}}}{}{}],
"contexts":[
{{"id":3,"def":"Context/Spawn"{},"blocks":[{{"id":30,"def":"Block/Burst","in":{{"Count":{{"value":[4]}}}}}}]}},
{{"id":4,"def":"Context/Initialize","flow":[[3,"In"]],"settings":{{"Capacity":16}},"blocks":[
{{"id":31,"def":"Block/SetAttribute","settings":{{"Attribute":"size"}},"in":{{"Size":{{"from":[10,"Out"]}}}}}}]}},
{{"id":5,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"blocks":[]}}{}{}]}})",
						   nodes.empty() ? "" : ",", nodes, spawn, extra.empty() ? "" : ",", extra);
	}

	str Hits(std::string_view name = "Hit") {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":100,"properties":[],"nodes":[],"contexts":[
{{"id":2,"def":"Context/Event","settings":{{"Event Name":"{}"}},"blocks":[]}},
{{"id":4,"def":"Context/Initialize","flow":[[2,"In"]],"settings":{{"Capacity":16}},"blocks":[]}},
{{"id":5,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"blocks":[]}}]}})",
						   name);
	}

	str Uses(u32 id, const Guid& asset, std::string_view rest = {}) {
		return std::format(R"({{"id":{},"def":"Context/Subgraph","settings":{{"asset":"{}"}}{},"blocks":[]}})", id, asset.ToString(), rest);
	}

	str Effect(std::string_view contexts, std::string_view properties = {}, std::string_view nodes = {}) {
		return std::format(R"({{"_type":"EffectGraph","version":4,"nextId":200,"properties":[{}],"nodes":[{}],"contexts":[{}]}})", properties, nodes,
						   contexts);
	}

	constexpr std::string_view OWN = R"({"id":3,"def":"Context/Spawn","blocks":[{"id":51,"def":"Block/ConstantSpawnRate","in":{"Rate":{"value":[3]}}}]},
{"id":4,"def":"Context/Initialize","flow":[[3,"In"]],"settings":{"Capacity":8},"blocks":[]},
{"id":5,"def":"Context/OutputParticlePrimitive","flow":[[4,"In"]],"blocks":[]})";

	constexpr std::string_view FAMILY = R"({"_type":"EffectGraph","version":4,"nextId":200,"properties":[],"nodes":[],"contexts":[
{"id":1,"def":"Context/Initialize","settings":{"Capacity":64},"blocks":[]},
{"id":2,"def":"Context/Update","flow":[[1,"In"]],"blocks":[{"id":20,"def":"Block/TriggerEvent","settings":{"Clamp To One":true,"Mode":"Always"},"in":{"Count":{"value":[1]}}}]},
{"id":5,"def":"Context/OutputParticlePrimitive","flow":[[2,"In"]],"blocks":[]},
{"id":6,"def":"Context/GPUEvent","in":{"Evt":{"from":[20,"Evt"]}},"blocks":[]},
{"id":3,"def":"Context/Initialize","flow":[[6,"In"]],"settings":{"Capacity":512},"blocks":[]},
{"id":7,"def":"Context/OutputParticlePrimitive","flow":[[3,"In"]],"blocks":[]}]})";

	Vector<str> Names(std::initializer_list<std::string_view> names) {
		Vector<str> out{GetMemoryResource(MemTag::Editor)};
		for (const std::string_view name : names) {
			out.push_back(str(name));
		}
		return out;
	}

	bool Has(const Vector<str>& list, std::string_view name) { return std::find(list.begin(), list.end(), name) != list.end(); }

	str SaltOf(const str& kernel) {
		const size_t call = kernel.rfind("ShalltearGraphRandom((");
		const size_t end = call == str::npos ? str::npos : kernel.find("u);", call);
		if (end == str::npos) {
			return {};
		}
		size_t begin = end;
		while (begin > 0 && kernel[begin - 1] >= '0' && kernel[begin - 1] <= '9') {
			--begin;
		}
		return kernel.substr(begin, end - begin);
	}

	class GraphEffectSystemSubgraphFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		EffectGraphStandIn _standIn;
		char _owner = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() / ("shalltear_effect_system_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetImporterRegistry::Register(_standIn.GetAssetType(), &_standIn, {GRAPH_EFFECT_EXTENSION}, &_owner);
			EffectSubgraphImporters::Register(&_owner);
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			AssetImporterRegistry::UnregisterOwnedBy(&_owner);
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid Add(const char* fileName, std::string_view text) {
			FileSystem::WriteTextFile(Path((_root / "Assets" / fileName).generic_string()), str(text));
			return AssetDatabase::EnsureAssetIdentity(Path(str("project://Assets/") + fileName));
		}

		static bool Compile(const str& text, GraphEffect& out, Vector<GraphDiagnostic>& diagnostics) {
			GraphDefinitions definitions;
			out = GraphEffect{};
			diagnostics.clear();
			return GraphEffectTarget::Compile(Parse(text), "User", out, diagnostics, &definitions);
		}

		static str Said(const Vector<GraphDiagnostic>& diagnostics) {
			str all;
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				all += std::format("[node {}] {}\n", diagnostic.node, diagnostic.message);
			}
			return all;
		}

		static bool Says(const Vector<GraphDiagnostic>& diagnostics, u32 node, std::string_view words) {
			return std::any_of(diagnostics.begin(), diagnostics.end(),
							   [&](const GraphDiagnostic& diagnostic) { return diagnostic.node == node && diagnostic.message.find(words) != str::npos; });
		}
	};

} // anonymous namespace

TEST_F(GraphEffectSystemSubgraphFixture, ContextHasPortsAndSlots) {
	const Guid burst = Add("Burst.sleffect", Burst(R"({"id":6,"def":"Context/Event","settings":{"Event Name":"Zap"},"blocks":[]},
{"id":7,"def":"Context/Event","settings":{"Event Name":"Hit"},"blocks":[]})"));
	ASSERT_TRUE(burst.IsValid());
	const GraphDocument user = Parse(Effect(Uses(1, burst)));
	GraphDefinitions definitions;
	str error;
	const GraphNodeDefinition* definition = definitions.Find(user, user.contexts[0].node, &error);
	ASSERT_NE(definition, nullptr) << error;
	EXPECT_EQ(definition->id, GRAPH_EFFECT_SYSTEM_SUBGRAPH);
	EXPECT_EQ(definition->name, "Burst");
	ASSERT_EQ(definition->inputs.size(), 2u);
	EXPECT_EQ(definition->inputs[0].name, "1");
	EXPECT_EQ(definition->inputs[0].label, "Size");
	EXPECT_FLOAT_EQ(definition->inputs[0].defaultValue[0], 0.5f);
	EXPECT_EQ(definition->inputs[1].label, "Tint");
	EXPECT_TRUE(definition->outputs.empty());
	EXPECT_FALSE(definition->flowOutput);
	const std::string_view expected[] = {"OnPlay", "OnStop", "Hit", "Zap"};
	EXPECT_TRUE(std::equal(definition->flowInputs.begin(), definition->flowInputs.end(), std::begin(expected), std::end(expected)));
	ASSERT_NE(definition->subgraph, nullptr);
	EXPECT_EQ(definition->subgraph->kind, GraphSubgraphKind::EffectSystem);

	const Guid linked = Add("Linked.sleffect", Burst(R"({"id":6,"def":"Context/Event","settings":{"Event Name":"Go"},"blocks":[]})",
													 R"(,"flow":[[6,"Start"],[6,"Stop"]])"));
	const GraphDocument other = Parse(Effect(Uses(1, linked)));
	definition = definitions.Find(other, other.contexts[0].node, &error);
	ASSERT_NE(definition, nullptr) << error;
	ASSERT_EQ(definition->flowInputs.size(), 1u);
	EXPECT_EQ(definition->flowInputs[0], "Go");
}

TEST_F(GraphEffectSystemSubgraphFixture, WrongKindSubgraphRefused) {
	const Guid burst = Add("Burst.sleffect", Burst());
	const Guid empty = Add("Empty.sleffectop", R"({"_type":"VisualEffectSubgraphOperator","version":1,"nextId":5,"properties":[],"nodes":[],"contexts":[]})");
	ASSERT_TRUE(burst.IsValid() && empty.IsValid());
	GraphDefinitions definitions;
	str error;
	const GraphDocument asNode = Parse(Effect({}, {}, std::format(R"({{"id":40,"def":"Utility/Subgraph","x":0,"y":0,"settings":{{"asset":"{}"}}}})", burst.ToString())));
	EXPECT_EQ(definitions.Find(asNode, asNode.nodes[0], &error), nullptr);
	EXPECT_NE(error.find("is an effect graph, where it takes an effect operator"), str::npos) << error;
	const GraphDocument asContext = Parse(Effect(Uses(1, empty)));
	EXPECT_EQ(definitions.Find(asContext, asContext.contexts[0].node, &error), nullptr);
	EXPECT_NE(error.find("is an effect operator, where it takes an effect graph"), str::npos) << error;

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(Effect(R"({"id":1,"def":"Context/Subgraph","blocks":[]})"), effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 1, "has no subgraph")) << Said(diagnostics);

	str events;
	for (u32 i = 0; i < 11; ++i) {
		events += std::format(R"(,{{"id":{},"def":"Context/Event","settings":{{"Event Name":"E{}"}},"blocks":[]}})", 60 + i, i);
	}
	const Guid many = Add("Many.sleffect", Burst(events.substr(1)));
	EXPECT_FALSE(Compile(Effect(Uses(1, many)), effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 1, "past the 10")) << Said(diagnostics);
}

TEST_F(GraphEffectSystemSubgraphFixture, UserCompilesSubgraphsSystemsAfterCopyUse) {
	const Guid burst = Add("Burst.sleffect", Burst());
	ASSERT_TRUE(burst.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(str(OWN) + "," + Uses(1, burst) + "," + Uses(2, burst)), effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.systems.size(), 3u);
	EXPECT_EQ(effect.systems[0].capacity, 8u);
	for (const u32 s : {1u, 2u}) {
		EXPECT_EQ(effect.systems[s].capacity, 16u);
		ASSERT_EQ(effect.systems[s].spawners.size(), 1u);
		const GraphEffectSpawner& spawner = effect.spawners[effect.systems[s].spawners[0]];
		EXPECT_EQ(spawner.start, Names({"OnPlay"}));
		EXPECT_EQ(spawner.stop, Names({"OnStop"}));
		EXPECT_NE(effect.systems[s].initializeKernel.find("in subgraph 'Burst'"), str::npos);
	}
	ASSERT_EQ(effect.spawners.size(), 3u);
	ASSERT_EQ(effect.systems[0].spawners.size(), 1u);
	EXPECT_NE(effect.systems[0].spawners[0], effect.systems[1].spawners[0]);
	EXPECT_NE(effect.systems[1].spawners[0], effect.systems[2].spawners[0]);
	EXPECT_EQ(effect.systems[0].initializeKernel.find("in subgraph"), str::npos);
}

TEST_F(GraphEffectSystemSubgraphFixture, SubgraphGpuEventJoinsSystems) {
	const Guid family = Add("Family.sleffect", FAMILY);
	ASSERT_TRUE(family.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(str(OWN) + "," + Uses(60, family)), effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.systems.size(), 3u);
	EXPECT_EQ(effect.systems[1].capacity, 64u);
	EXPECT_EQ(effect.systems[2].capacity, 512u);
	EXPECT_EQ(effect.systems[2].parent, 1u);
	ASSERT_EQ(effect.systems[1].updateEvents.size(), 1u);
	EXPECT_EQ(effect.systems[1].updateEvents[0].child, 2u);
	EXPECT_EQ(effect.systems[0].parent, GRAPH_EFFECT_NO_BINDING);
}

TEST_F(GraphEffectSystemSubgraphFixture, PortFeedsSubgraphsPropertyInEachUse) {
	const Guid burst = Add("Burst.sleffect", Burst());
	ASSERT_TRUE(burst.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(Uses(1, burst, R"(,"in":{"1":{"value":[0.75]}})") + "," + Uses(2, burst)), effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.systems.size(), 2u);
	EXPECT_NE(effect.systems[0].initializeKernel.find("0.75"), str::npos);
	EXPECT_EQ(effect.systems[0].initializeKernel.find("0.5,"), str::npos);
	EXPECT_NE(effect.systems[1].initializeKernel.find("0.5"), str::npos);
	EXPECT_TRUE(effect.properties.empty());

	const str wired = Effect(Uses(1, burst, R"(,"in":{"1":{"from":[40,"Out"]}})"), R"({"id":7,"name":"Big","reference":"Big","type":"Float","default":[3]})",
							 R"({"id":40,"def":"Input/Property","x":0,"y":0,"settings":{"property":7}})");
	ASSERT_TRUE(Compile(wired, effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.propertyFields.size(), 1u);
	ASSERT_EQ(effect.systems.size(), 1u);
	EXPECT_NE(effect.systems[0].initializeKernel.find(std::format("SltParticleEffectFloat({}u)", effect.propertyFields[0].offset)), str::npos);
}

TEST_F(GraphEffectSystemSubgraphFixture, EachUseDrawsIndependentRandom) {
	const Guid random = Add("Random.sleffect", Burst(R"({"id":8,"def":"Context/Update","flow":[[4,"In"]],"blocks":[
{"id":81,"def":"Block/SetAttribute","settings":{"Attribute":"size"},"in":{"Size":{"from":[11,"r"]}}}]})",
													 {}, R"({"id":11,"def":"Random/RandomNumber","x":0,"y":0,"settings":{"Constant":false}})"));
	ASSERT_TRUE(random.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(Uses(1, random) + "," + Uses(2, random)), effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.systems.size(), 2u);
	const str first = SaltOf(effect.systems[0].updateKernel);
	const str second = SaltOf(effect.systems[1].updateKernel);
	ASSERT_FALSE(first.empty());
	ASSERT_FALSE(second.empty());
	EXPECT_NE(first, second);
	GraphEffect again;
	ASSERT_TRUE(Compile(Effect(Uses(1, random) + "," + Uses(2, random)), again, diagnostics)) << Said(diagnostics);
	EXPECT_EQ(SaltOf(again.systems[0].updateKernel), first);
}

TEST_F(GraphEffectSystemSubgraphFixture, EventSlotStartsInnerSpawn) {
	const Guid burst = Add("Burst.sleffect", Burst());
	ASSERT_TRUE(burst.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	const str fired = Effect(R"({"id":9,"def":"Context/Event","settings":{"Event Name":"Fire"},"blocks":[]},)" + Uses(1, burst, R"(,"flow":[[9,"OnPlay"]])"));
	ASSERT_TRUE(Compile(fired, effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.spawners.size(), 1u);
	EXPECT_EQ(effect.spawners[0].start, Names({"Fire"}));
	EXPECT_EQ(effect.spawners[0].stop, Names({"OnStop"}));
	EXPECT_TRUE(Has(effect.events, "Fire"));

	ASSERT_TRUE(Compile(Effect(Uses(1, burst)), effect, diagnostics)) << Said(diagnostics);
	EXPECT_EQ(effect.spawners[0].start, Names({"OnPlay"}));
}

TEST_F(GraphEffectSystemSubgraphFixture, EventInsideIsWhatSlotCarriesIn) {
	const Guid hits = Add("Hits.sleffect", Hits());
	ASSERT_TRUE(hits.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(R"({"id":9,"def":"Context/Event","settings":{"Event Name":"Boom"},"blocks":[]},)" + Uses(1, hits, R"(,"flow":[[9,"Hit"]])")),
						effect, diagnostics))
		<< Said(diagnostics);
	ASSERT_EQ(effect.systems.size(), 1u);
	EXPECT_EQ(effect.systems[0].events, Names({"Boom"}));
	EXPECT_TRUE(Has(effect.events, "Boom"));
	EXPECT_FALSE(Has(effect.events, "Hit"));

	ASSERT_TRUE(Compile(Effect(Uses(1, hits)), effect, diagnostics)) << Said(diagnostics);
	EXPECT_EQ(effect.systems[0].events, Names({"Hit"}));
	EXPECT_TRUE(Has(effect.events, "Hit"));

	const str spawned = Effect(R"({"id":9,"def":"Context/Spawn","blocks":[{"id":90,"def":"Block/Burst","in":{"Count":{"value":[2]}}}]},)" +
							   Uses(1, hits, R"(,"flow":[[9,"Hit"]])"));
	ASSERT_TRUE(Compile(spawned, effect, diagnostics)) << Said(diagnostics);
	ASSERT_EQ(effect.spawners.size(), 1u);
	EXPECT_EQ(effect.spawners[0].context, 9u);
	EXPECT_EQ(effect.systems[0].spawners, (Vector<u32>{{0u}, GetMemoryResource(MemTag::Editor)}));
	EXPECT_TRUE(effect.systems[0].events.empty());
}

TEST_F(GraphEffectSystemSubgraphFixture, SpawnIntoSpawnRefused) {
	const Guid burst = Add("Burst.sleffect", Burst());
	ASSERT_TRUE(burst.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(Effect(R"({"id":9,"def":"Context/Spawn","blocks":[]},)" + Uses(1, burst, R"(,"flow":[[9,"OnPlay"]])")), effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 1, "in subgraph 'Burst', context 3 starts on 'OnPlay' (a Spawn")) << Said(diagnostics);

	EXPECT_FALSE(Compile(Effect(R"({"id":9,"def":"Context/GPUEvent","blocks":[]},)" + Uses(1, burst, R"(,"flow":[[9,"OnPlay"]])")), effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 1, "cannot feed")) << Said(diagnostics);
	EXPECT_FALSE(GraphEffectRules::MayFeed(GRAPH_EFFECT_INITIALIZE, GRAPH_EFFECT_SYSTEM_SUBGRAPH, "OnPlay"));
	EXPECT_TRUE(GraphEffectRules::MayFeed(GRAPH_EFFECT_EVENT, GRAPH_EFFECT_SYSTEM_SUBGRAPH, "Anything"));
}

TEST_F(GraphEffectSystemSubgraphFixture, StaleSlotLinkDropped) {
	const Guid burst = Add("Burst.sleffect", Burst());
	ASSERT_TRUE(burst.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(R"({"id":9,"def":"Context/Event","settings":{"Event Name":"Fire"},"blocks":[]},)" + Uses(1, burst, R"(,"flow":[[9,"Gone"]])")),
						effect, diagnostics))
		<< Said(diagnostics);
	EXPECT_EQ(effect.spawners[0].start, Names({"OnPlay"}));
}

TEST_F(GraphEffectSystemSubgraphFixture, NestedSlotLeftToUser) {
	const Guid inner = Add("Inner.sleffect", Hits());
	ASSERT_TRUE(inner.IsValid());
	const Guid outer = Add("Outer.sleffect", Effect(Uses(1, inner)));
	ASSERT_TRUE(outer.IsValid());
	const GraphDocument user = Parse(Effect(Uses(1, outer)));
	GraphDefinitions definitions;
	const GraphNodeDefinition* definition = definitions.Find(user, user.contexts[0].node);
	ASSERT_NE(definition, nullptr);
	ASSERT_EQ(definition->flowInputs.size(), 1u);
	EXPECT_EQ(definition->flowInputs[0], "Hit");

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(Compile(Effect(R"({"id":9,"def":"Context/Event","settings":{"Event Name":"Boom"},"blocks":[]},)" + Uses(1, outer, R"(,"flow":[[9,"Hit"]])")),
						effect, diagnostics))
		<< Said(diagnostics);
	ASSERT_EQ(effect.systems.size(), 1u);
	EXPECT_EQ(effect.systems[0].events, Names({"Boom"}));
	EXPECT_NE(effect.systems[0].initializeKernel.find("in subgraph 'Outer' in subgraph 'Inner'"), str::npos);

	ASSERT_TRUE(Compile(Effect(Uses(1, outer)), effect, diagnostics)) << Said(diagnostics);
	EXPECT_EQ(effect.systems[0].events, Names({"Hit"}));
}

TEST_F(GraphEffectSystemSubgraphFixture, InnerRefusalShownOnContext) {
	str broken = Burst();
	constexpr std::string_view Capacity = R"("Capacity":16)";
	broken.replace(broken.find(Capacity), Capacity.size(), R"("Capacity":0)");
	const Guid bad = Add("Bad.sleffect", broken);
	ASSERT_TRUE(bad.IsValid());
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(Effect(Uses(7, bad)), effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 7, "in subgraph 'Bad', context 4 has a Capacity of 0")) << Said(diagnostics);

	const Guid holder = Add("Holder.sleffect", Effect(Uses(11, bad)));
	EXPECT_FALSE(Compile(Effect(Uses(7, holder)), effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 7, "in subgraph 'Holder', context 11 in subgraph 'Bad', context 4 has a Capacity of 0")) << Said(diagnostics);

	str textured = Burst();
	constexpr std::string_view Tint = R"({"id":2,"name":"Tint","reference":"Tint","type":"Color","default":[1,0,0,1]})";
	textured.replace(textured.find(Tint), Tint.size(), R"({"id":2,"name":"Map","reference":"Map","type":"Texture2D"})");
	const Guid mapped = Add("Mapped.sleffect", textured);
	EXPECT_FALSE(Compile(Effect(Uses(7, mapped)), effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 7, "in subgraph 'Mapped', property 'Map' is a texture")) << Said(diagnostics);
}

TEST_F(GraphEffectSystemSubgraphFixture, SubgraphCycleRefused) {
	const Path selfPath("project://Assets/Self.sleffect");
	FileSystem::WriteTextFile(Path((_root / "Assets" / "Self.sleffect").generic_string()), str(Burst()));
	const Guid self = AssetDatabase::EnsureAssetIdentity(selfPath);
	ASSERT_TRUE(self.IsValid());
	const str selfText = Burst(Uses(20, self));
	FileSystem::WriteTextFile(Path((_root / "Assets" / "Self.sleffect").generic_string()), selfText);
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_FALSE(Compile(selfText, effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 20, "'Self' -> 'Self'")) << Said(diagnostics);

	FileSystem::WriteTextFile(Path((_root / "Assets" / "A.sleffect").generic_string()), str(Burst()));
	const Guid a = AssetDatabase::EnsureAssetIdentity(Path("project://Assets/A.sleffect"));
	const Guid b = Add("B.sleffect", Burst(Uses(21, a)));
	ASSERT_TRUE(a.IsValid() && b.IsValid());
	const str aText = Burst(Uses(22, b));
	FileSystem::WriteTextFile(Path((_root / "Assets" / "A.sleffect").generic_string()), aText);
	EXPECT_FALSE(Compile(aText, effect, diagnostics));
	EXPECT_TRUE(Says(diagnostics, 22, "'B' -> 'A' -> 'B'")) << Said(diagnostics);
}

TEST_F(GraphEffectSystemSubgraphFixture, EditsUseEffectGraphButNeverCycle) {
	const Guid burst = Add("Burst.sleffect", Burst());
	const Guid user = Add("User.sleffect", Effect(Uses(1, burst)));
	const Guid doubler = Add("Doubler.sleffectop", R"({"_type":"VisualEffectSubgraphOperator","version":1,"nextId":5,"properties":[],"nodes":[],"contexts":[]})");
	ASSERT_TRUE(burst.IsValid() && user.IsValid() && doubler.IsValid());
	GraphDefinitions definitions;
	GraphDocument editing = Parse(Effect(R"({"id":9,"def":"Context/Event","settings":{"Event Name":"Fire"},"blocks":[]},
{"id":8,"def":"Context/Spawn","blocks":[]},{"id":7,"def":"Context/GPUEvent","blocks":[]},
{"id":6,"def":"Context/Initialize","settings":{"Capacity":4},"blocks":[]})"));
	EXPECT_EQ(EffectGraphEditOps::AddSystemSubgraph(editing, burst, {}, definitions, burst), 0u);
	EXPECT_EQ(EffectGraphEditOps::AddSystemSubgraph(editing, user, {}, definitions, burst), 0u);
	EXPECT_EQ(EffectGraphEditOps::AddSystemSubgraph(editing, doubler, {}, definitions), 0u);
	GraphDocument block = Parse(EffectGraphEditOps::BlockSubgraphTemplate());
	EXPECT_FALSE(EffectGraphEditOps::CanUseSystemSubgraph(block, burst, definitions));

	const u32 context = EffectGraphEditOps::AddSystemSubgraph(editing, burst, Vec2(10.0f, 20.0f), definitions);
	ASSERT_NE(context, 0u);
	EXPECT_EQ(GraphSubgraphs::AssetOf(editing.contexts.back().node), burst);
	EXPECT_TRUE(EffectGraphEditOps::FlowInputs(editing, context).empty());
	const auto slots = EffectGraphEditOps::FlowInputs(editing, context, &definitions);
	ASSERT_EQ(slots.size(), 2u);
	EXPECT_TRUE(EffectGraphEditOps::CanLink(editing, 9, context, "OnPlay", nullptr, &definitions));
	EXPECT_TRUE(EffectGraphEditOps::CanLink(editing, 8, context, "OnStop", nullptr, &definitions));
	EXPECT_FALSE(EffectGraphEditOps::CanLink(editing, 7, context, "OnPlay", nullptr, &definitions));
	EXPECT_FALSE(EffectGraphEditOps::CanLink(editing, 6, context, "OnPlay", nullptr, &definitions));
	EXPECT_FALSE(EffectGraphEditOps::CanLink(editing, 9, context, "Gone", nullptr, &definitions));
	EXPECT_FALSE(EffectGraphEditOps::FlowOutput(editing, context));
	ASSERT_TRUE(EffectGraphEditOps::Link(editing, 9, context, "OnPlay", &definitions));
	ASSERT_TRUE(EffectGraphEditOps::Link(editing, 8, context, "OnPlay", &definitions));
	Vector<EffectGraphEditOps::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	EffectGraphEditOps::BlockChoices(editing, context, choices);
	EXPECT_TRUE(choices.empty());
}

#ifndef SHALLTEAR_SHARED

namespace {

	class GraphEffectSystemSubgraphReadbackFixture : public EffectKernelHarness::GraphEffectSystemsFixture {
	protected:
		EffectGraphStandIn _standIn;
		char _owner = 0;

		void SetUp() override {
			EffectKernelHarness::GraphEffectSystemsFixture::SetUp();
			AssetImporterRegistry::Register(_standIn.GetAssetType(), &_standIn, {GRAPH_EFFECT_EXTENSION}, &_owner);
		}
		void TearDown() override {
			AssetImporterRegistry::UnregisterOwnedBy(&_owner);
			EffectKernelHarness::GraphEffectSystemsFixture::TearDown();
		}
		Guid Add(const char* fileName, std::string_view text) {
			FileSystem::WriteTextFile(Path((_root / fileName).generic_string()), str(text));
			return AssetDatabase::EnsureAssetIdentity(Path(str("project://") + fileName));
		}
	};

} // anonymous namespace

TEST_F(GraphEffectSystemSubgraphReadbackFixture, SubgraphsSystemSpawnsInUserUseEach) {
	const Guid burst = Add("Burst.sleffect", Burst());
	ASSERT_TRUE(burst.IsValid());
	const str user = Effect(Uses(1, burst, R"(,"in":{"1":{"value":[0.25]}})") + "," + Uses(2, burst, R"(,"in":{"1":{"value":[0.75]}})"));
	EffectKernelHarness::SystemsRun run;
	if (!Begin(run, user, "SystemSubgraph")) {
		GTEST_SKIP();
	}
	ASSERT_EQ(run.systems.size(), 2u);
	Advance(run, 0.0f, 1.0f / 60.0f, {4, 4});
	const f32 expected[] = {0.25f, 0.75f};
	for (u32 s = 0; s < 2; ++s) {
		const Vector<f32> sizes = Values(run, s, "size");
		ASSERT_EQ(sizes.size(), 4u) << "use " << s;
		for (const f32 size : sizes) {
			EXPECT_FLOAT_EQ(size, expected[s]) << "use " << s;
		}
	}
}

#endif
