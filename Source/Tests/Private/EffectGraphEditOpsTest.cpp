#include "TestsPCH.h"

#include "EffectGraphEditOps.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphNodeLibrary.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace Shalltear;

namespace {

	namespace Ops = EffectGraphEditOps;

	GraphDocument Empty() {
		GraphDocument document;
		document.type = str(GRAPH_EFFECT_DOCUMENT_TYPE);
		return document;
	}

	const GraphContext* Context(const GraphDocument& document, u32 id) {
		for (const GraphContext& context : document.contexts) {
			if (context.node.id == id) {
				return &context;
			}
		}
		return nullptr;
	}

	Ops::BlockChoice Choice(const GraphDocument& document, u32 context, std::string_view name) {
		Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		Ops::BlockChoices(document, context, choices);
		for (const Ops::BlockChoice& choice : choices) {
			if (choice.name == name) {
				return choice;
			}
		}
		ADD_FAILURE() << "block '" << name << "' not offered";
		return {};
	}

	bool Offered(const GraphDocument& document, u32 context, std::string_view name) {
		Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		Ops::BlockChoices(document, context, choices);
		return std::any_of(choices.begin(), choices.end(), [&](const Ops::BlockChoice& choice) { return choice.name == name; });
	}

	bool OperatorOffered(std::string_view id) {
		const GraphNodeDefinition* definition = GraphNodeLibrary::Find(id);
		EXPECT_NE(definition, nullptr) << id;
		return definition && Ops::IsOperator(*definition);
	}

} // anonymous namespace

TEST(EffectGraphEditOpsTest, ContextIsMadeWithDefinitionsFlow) {
	GraphDocument document = Empty();
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2(10.4f, 20.6f));
	ASSERT_NE(spawn, 0u);
	EXPECT_EQ(Context(document, spawn)->node.x, 10);
	EXPECT_EQ(Context(document, spawn)->node.y, 21);
	EXPECT_EQ(Ops::FlowInputs(document, spawn).size(), 2u);
	EXPECT_TRUE(Ops::FlowOutput(document, spawn));
	const u32 output = Ops::AddContext(document, GRAPH_EFFECT_OUTPUT, Vec2());
	EXPECT_FALSE(Ops::FlowOutput(document, output));
	EXPECT_EQ(Ops::AddContext(document, "Math/Add", Vec2()), 0u);
	EXPECT_EQ(Ops::AddContext(document, "Context/Fragment", Vec2()), 0u);
	u32 defined = 0;
	for (const GraphNodeDefinition* definition : GraphNodeLibrary::All()) {
		if (!GraphEffectRules::IsContext(definition->id) || definition->id == GRAPH_EFFECT_SYSTEM_SUBGRAPH) {
			continue;
		}
		++defined;
		const auto contexts = Ops::Contexts();
		EXPECT_EQ(std::count(contexts.begin(), contexts.end(), definition->id), 1);
	}
	EXPECT_EQ(Ops::Contexts().size(), defined);
	EXPECT_GE(defined, 12u);
	for (std::string_view kind : {GRAPH_EFFECT_OUTPUT, GRAPH_EFFECT_OUTPUT_MESH, GRAPH_EFFECT_OUTPUT_POINT, GRAPH_EFFECT_OUTPUT_LINE,
								  GRAPH_EFFECT_OUTPUT_LIT, GRAPH_EFFECT_OUTPUT_LIT_MESH, GRAPH_EFFECT_OUTPUT_NODE_SHADER,
								  GRAPH_EFFECT_OUTPUT_NODE_SHADER_MESH}) {
		const u32 made = Ops::AddContext(document, kind, Vec2());
		ASSERT_NE(made, 0u) << kind;
		EXPECT_EQ(Ops::FlowInputs(document, made).size(), 1u) << kind;
		EXPECT_FALSE(Ops::FlowOutput(document, made)) << kind;
		EXPECT_TRUE(GraphEffectRules::IsOutput(kind)) << kind;
	}
	EXPECT_FALSE(GraphEffectRules::IsOutput(GRAPH_EFFECT_UPDATE));
}

TEST(EffectGraphEditOpsTest, LinksFollowCompilerRules) {
	GraphDocument document = Empty();
	const u32 event = Ops::AddContext(document, GRAPH_EFFECT_EVENT, Vec2());
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	const u32 initialize = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	const u32 other = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	const u32 update = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	const u32 second = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	const u32 output = Ops::AddContext(document, GRAPH_EFFECT_OUTPUT, Vec2());

	EXPECT_TRUE(Ops::Link(document, event, spawn, GRAPH_EFFECT_STOP));
	EXPECT_TRUE(Ops::Link(document, spawn, initialize, GRAPH_EFFECT_IN));
	EXPECT_TRUE(Ops::Link(document, initialize, update, GRAPH_EFFECT_IN));
	EXPECT_TRUE(Ops::Link(document, update, output, GRAPH_EFFECT_IN));

	str why;
	EXPECT_FALSE(Ops::CanLink(document, spawn, update, GRAPH_EFFECT_IN, &why));
	EXPECT_FALSE(why.empty());
	EXPECT_TRUE(Ops::CanLink(document, event, initialize, GRAPH_EFFECT_IN));
	const u32 chained = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	EXPECT_FALSE(Ops::CanLink(document, spawn, chained, GRAPH_EFFECT_START));
	EXPECT_FALSE(Ops::CanLink(document, spawn, initialize, GRAPH_EFFECT_START));
	EXPECT_FALSE(Ops::CanLink(document, output, update, GRAPH_EFFECT_IN));
	EXPECT_FALSE(Ops::CanLink(document, spawn, initialize, GRAPH_EFFECT_IN));
	EXPECT_FALSE(Ops::CanLink(document, initialize, second, GRAPH_EFFECT_IN, &why));
	EXPECT_NE(why.find("leads to an Update"), str::npos) << why;

	ASSERT_TRUE(Ops::Link(document, other, update, GRAPH_EFFECT_IN));
	const GraphContext* updated = Context(document, update);
	ASSERT_EQ(updated->flows.size(), 1u);
	EXPECT_EQ(updated->flows[0].from, other);
	ASSERT_TRUE(Ops::Link(document, Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2()), initialize, GRAPH_EFFECT_IN));
	EXPECT_EQ(Context(document, initialize)->flows.size(), 2u);

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	EXPECT_TRUE(GraphEffectTarget::Compile(document, "links.sleffect", effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
}

TEST(EffectGraphEditOpsTest, StackOffersBlocksItHoldsAsVariants) {
	GraphDocument document = Empty();
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	const u32 initialize = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	const u32 update = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	const u32 event = Ops::AddContext(document, GRAPH_EFFECT_EVENT, Vec2());

	Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	Ops::BlockChoices(document, spawn, choices);
	ASSERT_GE(choices.size(), 3u);
	EXPECT_EQ(choices[0].name, "Constant Spawn Rate");
	EXPECT_EQ(choices[2].name, "Periodic Burst");
	EXPECT_TRUE(Offered(document, spawn, "Set SpawnEvent Lifetime"));
	EXPECT_TRUE(Offered(document, spawn, "Set SpawnEvent Color"));
	EXPECT_FALSE(Offered(document, spawn, "Set SpawnEvent ParticleId"));
	Ops::BlockChoices(document, event, choices);
	EXPECT_TRUE(choices.empty());

	EXPECT_TRUE(Offered(document, initialize, "Set Velocity"));
	EXPECT_TRUE(Offered(document, initialize, "Add Velocity"));
	EXPECT_TRUE(Offered(document, update, "Blend Color"));
	EXPECT_TRUE(Offered(document, initialize, "Set Alive"));
	EXPECT_FALSE(Offered(document, initialize, "Add Alive"));
	EXPECT_TRUE(Offered(document, initialize, "Set Position from Source"));
	EXPECT_FALSE(Offered(document, update, "Set Position from Source"));
	EXPECT_FALSE(Offered(document, spawn, "Set Velocity"));
	EXPECT_TRUE(Offered(document, initialize, "Set Lifetime Random (Uniform)"));
	EXPECT_TRUE(Offered(document, update, "Add Velocity Random (Per Component)"));
	EXPECT_FALSE(Offered(document, initialize, "Set Lifetime Random (Per Component)"));
	EXPECT_FALSE(Offered(document, initialize, "Set Alive Random (Uniform)"));
	EXPECT_FALSE(Offered(document, initialize, "Set ParticleId"));

	const u32 lifetime = Ops::AddBlock(document, initialize, 0, Choice(document, initialize, "Set Lifetime"));
	const u32 add = Ops::AddBlock(document, initialize, 0, Choice(document, initialize, "Add Velocity"));
	ASSERT_NE(lifetime, 0u);
	ASSERT_NE(add, 0u);
	const GraphContext* stack = Context(document, initialize);
	ASSERT_EQ(stack->blocks.size(), 2u);
	EXPECT_EQ(stack->blocks[0].id, add);
	GraphDefinitions definitions;
	const GraphNodeDefinition* built = definitions.Find(document, stack->blocks[0]);
	ASSERT_NE(built, nullptr);
	EXPECT_EQ(built->name, "Add Velocity");
	EXPECT_EQ(Ops::AddBlock(document, spawn, 0, Choice(document, initialize, "Set Lifetime")), 0u);
}

TEST(EffectGraphEditOpsTest, AttributeFromCurveIsOfferedAsVariants) {
	GraphDocument document = Empty();
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	const u32 update = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	const u32 output = Ops::AddContext(document, GRAPH_EFFECT_OUTPUT, Vec2());

	EXPECT_TRUE(Offered(document, update, "Set Size Over Life"));
	EXPECT_TRUE(Offered(document, output, "Multiply Color Over Life"));
	EXPECT_TRUE(Offered(document, update, "Add Velocity Random from Curve"));
	EXPECT_TRUE(Offered(document, update, "Blend Size By Speed"));
	EXPECT_TRUE(Offered(document, update, "Set Age By Speed"));
	EXPECT_FALSE(Offered(document, update, "Set Age Over Life"));
	EXPECT_FALSE(Offered(document, update, "Add Velocity By Speed"));
	EXPECT_FALSE(Offered(document, update, "Set Alive Over Life"));
	EXPECT_FALSE(Offered(document, update, "Set Size Custom"));
	EXPECT_FALSE(Offered(document, spawn, "Set Size Over Life"));

	Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	Ops::BlockChoices(document, update, choices);
	EXPECT_EQ(std::count_if(choices.begin(), choices.end(), [](const Ops::BlockChoice& choice) { return choice.name == "Set Size Over Life"; }), 1);

	const u32 made = Ops::AddBlock(document, output, 0, Choice(document, output, "Multiply Color Over Life"));
	ASSERT_NE(made, 0u);
	GraphDefinitions definitions;
	const GraphNodeDefinition* built = definitions.Find(document, Context(document, output)->blocks[0]);
	ASSERT_NE(built, nullptr);
	EXPECT_EQ(built->name, "Multiply Color Over Life");
}

TEST(EffectGraphEditOpsTest, BlockMovesToRunningContext) {
	GraphDocument document = Empty();
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	const u32 initialize = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	const u32 update = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	const u32 rate = Ops::AddBlock(document, spawn, 0, Choice(document, spawn, "Constant Spawn Rate"));
	const u32 velocity = Ops::AddBlock(document, initialize, 0, Choice(document, initialize, "Set Velocity"));

	const u32 particle[] = {velocity};
	const u32 mixed[] = {velocity, rate};
	EXPECT_TRUE(Ops::CanMoveBlocks(document, particle, update));
	EXPECT_FALSE(Ops::CanMoveBlocks(document, particle, spawn));
	EXPECT_FALSE(Ops::CanMoveBlocks(document, mixed, update));
	ASSERT_TRUE(Ops::MoveBlocks(document, particle, update, 0));
	EXPECT_TRUE(Context(document, initialize)->blocks.empty());
	EXPECT_EQ(Context(document, update)->blocks.size(), 1u);
	EXPECT_FALSE(Ops::MoveBlocks(document, mixed, initialize, 0));
}

TEST(EffectGraphEditOpsTest, DroppedOperatorMatchesKernel) {
	EXPECT_TRUE(OperatorOffered("Math/Add"));
	EXPECT_TRUE(OperatorOffered("Math/Multiply"));
	EXPECT_TRUE(OperatorOffered("Input/Time"));
	EXPECT_TRUE(OperatorOffered("Math/Sine"));
	EXPECT_TRUE(OperatorOffered(GRAPH_EFFECT_SAMPLE_CURVE));
	EXPECT_TRUE(OperatorOffered("Input/SampleGradient"));
	EXPECT_FALSE(OperatorOffered("Math/DDX"));
	EXPECT_FALSE(OperatorOffered("Input/Property"));
	EXPECT_FALSE(OperatorOffered(GraphDocumentOps::REDIRECT_DEF));
	EXPECT_FALSE(OperatorOffered(GRAPH_EFFECT_SET_ATTRIBUTE));
	EXPECT_FALSE(OperatorOffered(GRAPH_EFFECT_INITIALIZE));
	EXPECT_FALSE(OperatorOffered("Block/BaseColor"));

	u32 offered = 0;
	for (const GraphNodeDefinition* definition : GraphNodeLibrary::All()) {
		offered += Ops::IsOperator(*definition) ? 1u : 0u;
	}
	EXPECT_GT(offered, 30u);
}

TEST(EffectGraphEditOpsTest, TheBlackboardOffersWhatAnEffectHolds) {
	const auto shader = NodeShaderEditOps::PropertyTypes();
	for (std::string_view type : Ops::PropertyTypes()) {
		EXPECT_NE(std::find(shader.begin(), shader.end(), type), shader.end()) << type;
		GraphDocument document = Empty();
		EXPECT_NE(NodeShaderEditOps::AddProperty(document, type), 0u) << type;
	}
	EXPECT_EQ(std::find(Ops::PropertyTypes().begin(), Ops::PropertyTypes().end(), std::string_view("Texture2D")), Ops::PropertyTypes().end());
}

TEST(EffectGraphEditOpsTest, ContextIsDeletedAndCopiedWithBlocks) {
	GraphDocument document = Empty();
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	const u32 initialize = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	Ops::Link(document, spawn, initialize, GRAPH_EFFECT_IN);
	Ops::AddBlock(document, initialize, 0, Choice(document, initialize, "Set Lifetime"));
	const u32 property = NodeShaderEditOps::AddProperty(document, "Float");
	const u32 reader = NodeShaderEditOps::AddPropertyNode(document, property, Vec2());

	const u32 copied[] = {initialize, reader};
	const str fragment = Ops::Copy(document, copied);
	GraphDocument read;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(fragment, read, error)) << error;
	ASSERT_EQ(read.contexts.size(), 1u);
	EXPECT_EQ(read.contexts[0].blocks.size(), 1u);
	EXPECT_EQ(read.properties.size(), 1u);

	const u32 doomed[] = {spawn};
	EXPECT_EQ(Ops::Delete(document, doomed), 1u);
	EXPECT_TRUE(Context(document, initialize)->flows.empty());
}

TEST(EffectGraphEditOpsTest, LoneBlockCopyLandsInStack) {
	GraphDocument document = Empty();
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2());
	const u32 initialize = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2());
	const u32 update = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2());
	const u32 rate = Ops::AddBlock(document, spawn, 0, Choice(document, spawn, "Constant Spawn Rate"));
	const u32 lifetime = Ops::AddBlock(document, initialize, 0, Choice(document, initialize, "Set Lifetime"));
	const u32 velocity = Ops::AddBlock(document, initialize, 1, Choice(document, initialize, "Set Velocity"));
	const u32 resident = Ops::AddBlock(document, update, 0, Choice(document, update, "Set Velocity"));
	ASSERT_TRUE(rate && lifetime && velocity && resident);
	GraphNode feeder;
	feeder.id = document.nextId++;
	feeder.def = "Math/Add";
	document.nodes.push_back(feeder);
	GraphContext& holder = *const_cast<GraphContext*>(Context(document, initialize));
	holder.blocks[0].inputs.push_back({"Lifetime", 0, "", "[3]", ""});
	holder.blocks[1].inputs.push_back({"Velocity", feeder.id, "Out", "[0,1,0]", ""});

	const u32 named[] = {velocity, lifetime};
	const str fragment = Ops::Copy(document, named);
	ASSERT_TRUE(Ops::IsBlockFragment(fragment));
	GraphDocument read;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(fragment, read, error)) << error;
	EXPECT_TRUE(read.contexts.empty());
	ASSERT_EQ(read.nodes.size(), 2u);
	EXPECT_EQ(read.nodes[0].id, velocity);
	ASSERT_EQ(read.nodes[0].inputs.size(), 1u);
	EXPECT_EQ(read.nodes[0].inputs[0].fromNode, 0u);
	EXPECT_EQ(read.nodes[0].inputs[0].value, "[0,1,0]");
	EXPECT_EQ(read.nodes[1].inputs[0].value, "[3]");

	Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
	const u32 below[] = {resident};
	ASSERT_TRUE(Ops::PasteBlocks(document, fragment, below, pasted, error)) << error;
	ASSERT_EQ(pasted.size(), 2u);
	const GraphContext* updated = Context(document, update);
	ASSERT_EQ(updated->blocks.size(), 3u);
	EXPECT_EQ(updated->blocks[0].id, resident);
	EXPECT_EQ(updated->blocks[1].id, pasted[0]);
	EXPECT_EQ(updated->blocks[2].id, pasted[1]);
	EXPECT_EQ(updated->blocks[1].def, GRAPH_EFFECT_SET_ATTRIBUTE);
	EXPECT_TRUE(pasted[0] != velocity && pasted[1] != lifetime);
	const u32 both[] = {velocity, lifetime};
	ASSERT_TRUE(Ops::PasteBlocks(document, fragment, both, pasted, error)) << error;
	ASSERT_EQ(Context(document, initialize)->blocks.size(), 4u);
	EXPECT_EQ(Context(document, initialize)->blocks[1].id, velocity);
	EXPECT_EQ(Context(document, initialize)->blocks[2].id, pasted[0]);
	const u32 stack[] = {update};
	ASSERT_TRUE(Ops::PasteBlocks(document, fragment, stack, pasted, error)) << error;
	ASSERT_EQ(Context(document, update)->blocks.size(), 5u);
	EXPECT_EQ(Context(document, update)->blocks[4].id, pasted[1]);

	const u32 mixed[] = {rate, lifetime};
	const str kinds = Ops::Copy(document, mixed);
	ASSERT_TRUE(Ops::PasteBlocks(document, kinds, stack, pasted, error)) << error;
	EXPECT_EQ(pasted.size(), 1u);
	const u32 spawning[] = {spawn};
	EXPECT_FALSE(Ops::PasteBlocks(document, fragment, spawning, pasted, error));
	EXPECT_TRUE(Context(document, spawn)->blocks.size() == 1u && pasted.empty());
	const u32 two[] = {spawn, update};
	EXPECT_FALSE(Ops::PasteBlocks(document, fragment, two, pasted, error));
	EXPECT_EQ(error, "Select one block or one context to paste the blocks into.");
	EXPECT_FALSE(Ops::PasteBlocks(document, fragment, {}, pasted, error));

	const u32 beside[] = {lifetime, feeder.id};
	const str nodes = Ops::Copy(document, beside);
	EXPECT_FALSE(Ops::IsBlockFragment(nodes));
	ASSERT_TRUE(GraphDocumentOps::Read(nodes, read, error)) << error;
	EXPECT_EQ(read.nodes.size(), 1u);
	EXPECT_FALSE(Ops::PasteBlocks(document, nodes, stack, pasted, error));
}

TEST(EffectGraphEditOpsTest, EmptyGraphBecomesFountainTargetCompiles) {
	GraphDocument document = Empty();
	const u32 spawn = Ops::AddContext(document, GRAPH_EFFECT_SPAWN, Vec2(0.0f, 0.0f));
	const u32 initialize = Ops::AddContext(document, GRAPH_EFFECT_INITIALIZE, Vec2(0.0f, 200.0f));
	const u32 update = Ops::AddContext(document, GRAPH_EFFECT_UPDATE, Vec2(0.0f, 500.0f));
	const u32 output = Ops::AddContext(document, GRAPH_EFFECT_OUTPUT, Vec2(0.0f, 700.0f));
	ASSERT_TRUE(Ops::Link(document, spawn, initialize, GRAPH_EFFECT_IN));
	ASSERT_TRUE(Ops::Link(document, initialize, update, GRAPH_EFFECT_IN));
	ASSERT_TRUE(Ops::Link(document, update, output, GRAPH_EFFECT_IN));

	const u32 rate = Ops::AddBlock(document, spawn, 0, Choice(document, spawn, "Constant Spawn Rate"));
	const u32 velocity = Ops::AddBlock(document, initialize, 0, Choice(document, initialize, "Set Velocity"));
	const u32 lifetime = Ops::AddBlock(document, initialize, 1, Choice(document, initialize, "Set Lifetime"));
	const u32 gravity = Ops::AddBlock(document, update, 0, Choice(document, update, "Add Velocity"));
	const f32 up[] = {0.0f, 4.0f, 0.0f};
	const f32 two[] = {2.0f};
	const f32 pull[] = {0.0f, -0.15f, 0.0f};
	const f32 forty[] = {40.0f};
	ASSERT_TRUE(NodeShaderEditOps::SetInputConstant(document, velocity, "Velocity", up));
	ASSERT_TRUE(NodeShaderEditOps::SetInputConstant(document, lifetime, "Lifetime", two));
	ASSERT_TRUE(NodeShaderEditOps::SetInputConstant(document, gravity, "Velocity", pull));
	ASSERT_TRUE(NodeShaderEditOps::SetInputConstant(document, rate, "Rate", forty));
	ASSERT_TRUE(NodeShaderEditOps::SetNodeSetting(document, initialize, "Capacity", "256"));

	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(document, "fountain.sleffect", effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	ASSERT_EQ(effect.systems.size(), 1u);
	EXPECT_EQ(effect.systems[0].capacity, 256u);
	EXPECT_EQ(effect.systems[0].update, update);
	ASSERT_EQ(effect.spawners.size(), 1u);
	EXPECT_EQ(effect.spawners[0].blocks.size(), 1u);
	EXPECT_EQ(effect.systems[0].outputs.size(), 1u);
}

TEST(EffectGraphEditOpsTest, SearcherOffersPerAttributeEntries) {
	Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
	Ops::OperatorChoices(choices);
	auto find = [&](std::string_view name) -> const Ops::BlockChoice* {
		for (const Ops::BlockChoice& choice : choices) {
			if (choice.name == name) {
				return &choice;
			}
		}
		return nullptr;
	};
	ASSERT_NE(find("Get Attribute: age"), nullptr);
	ASSERT_NE(find("Get Attribute: particleId"), nullptr);
	ASSERT_NE(find("Random Number (Vector3)"), nullptr);
	EXPECT_TRUE(Ops::IsOfferedAsVariants(GRAPH_EFFECT_GET_ATTRIBUTE));
	EXPECT_FALSE(Ops::IsOfferedAsVariants(GRAPH_EFFECT_AGE_OVER_LIFETIME));

	GraphDocument document = Empty();
	const u32 age = Ops::AddOperator(document, *find("Get Attribute: age"), Vec2());
	const u32 random = Ops::AddOperator(document, *find("Random Number (Vector3)"), Vec2(0.0f, 200.0f));
	ASSERT_NE(age, 0u);
	ASSERT_NE(random, 0u);
	GraphDefinitions definitions;
	const GraphNodeDefinition* reader = definitions.Find(document, *GraphDocumentOps::FindNode(document, age));
	ASSERT_NE(reader, nullptr);
	ASSERT_EQ(reader->outputs.size(), 1u);
	EXPECT_EQ(reader->outputs[0].name, "age");
	EXPECT_EQ(reader->name, "Get Attribute: age");
	const GraphNodeDefinition* draw = definitions.Find(document, *GraphDocumentOps::FindNode(document, random));
	ASSERT_NE(draw, nullptr);
	EXPECT_EQ(draw->outputs[0].type, GraphPortType::Float3);
	Ops::BlockChoice block;
	block.def = str(GRAPH_EFFECT_SET_ATTRIBUTE);
	EXPECT_EQ(Ops::AddOperator(document, block, Vec2()), 0u);
}

TEST(EffectGraphEditOpsTest, TemplatesAreGraphsEffectTargetCompiles) {
	ASSERT_EQ(Ops::TemplateCount(), 5u);
	EXPECT_EQ(Ops::TemplateLabel(0), "Visual Effects/Empty");
	EXPECT_EQ(Ops::TemplateLabel(1), "Visual Effects/Simple Loop");
	EXPECT_EQ(Ops::TemplateLabel(2), "Visual Effects/Simple Burst");
	EXPECT_EQ(Ops::TemplateLabel(3), "Visual Effects/Simple Trail");
	EXPECT_EQ(Ops::TemplateLabel(4), "Visual Effects/Head & Trail");
	EXPECT_TRUE(Ops::TemplateLabel(5).empty());
	EXPECT_TRUE(Ops::TemplateDocument(5).empty());

	for (u32 index = 0; index < 3; ++index) {
		GraphDocument document;
		str error;
		ASSERT_TRUE(GraphDocumentOps::Read(Ops::TemplateDocument(index), document, error)) << error;
		EXPECT_EQ(document.type, GRAPH_EFFECT_DOCUMENT_TYPE);
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(GraphEffectTarget::Compile(document, "New Effect.sleffect", effect, diagnostics)) << Ops::TemplateLabel(index) << ' ' << (diagnostics.empty() ? str() : diagnostics[0].message);
		if (index == 0) {
			EXPECT_TRUE(document.contexts.empty());
			EXPECT_TRUE(effect.systems.empty());
			continue;
		}
		const bool burst = index == 2;
		const str name(Ops::TemplateLabel(index));
		ASSERT_EQ(effect.systems.size(), 1u);
		const GraphEffectSystem& system = effect.systems[0];
		EXPECT_EQ(system.capacity, burst ? 128u : 68u) << name;
		EXPECT_NE(system.update, 0u) << name;
		EXPECT_EQ(system.outputs.size(), 1u);
		ASSERT_EQ(effect.spawners.size(), 1u);
		ASSERT_EQ(effect.spawners[0].blocks.size(), 1u);
		EXPECT_EQ(effect.spawners[0].blocks[0].kind, burst ? GraphEffectSpawnBlock::Kind::SingleBurst : GraphEffectSpawnBlock::Kind::ConstantRate)
			<< name;
		ASSERT_EQ(effect.spawners[0].loop.roots.size(), 4u);
		const GraphProgram& loop = effect.spawners[0].loop;
		EXPECT_EQ(loop.ConstantOf(loop.roots[0].value)[0], burst ? 1.0f : -1.0f) << name;
		EXPECT_EQ(loop.ConstantOf(loop.roots[1].value)[0], -1.0f) << name;
		const auto names = [&](u32 id) {
			str out;
			GraphDefinitions definitions;
			for (const GraphContext& context : document.contexts) {
				if (context.node.id != id) {
					continue;
				}
				for (const GraphNode& block : context.blocks) {
					const GraphNodeDefinition* definition = definitions.Find(document, block);
					out += (out.empty() ? "" : ", ") + (definition ? str(definition->name) : block.def);
				}
			}
			return out;
		};
		EXPECT_EQ(names(system.initialize), "Set Lifetime Random (Uniform), Set Position Shape Sphere, Set Velocity") << name;
		EXPECT_EQ(names(system.update), "Gravity, Linear Drag") << name;
		EXPECT_NE(system.initializeKernel.find("ShalltearGraphRandom"), str::npos) << name;
		GraphDefinitions definitions;
		const GraphContext* shown = nullptr;
		for (const GraphContext& context : document.contexts) {
			shown = context.node.def == GRAPH_EFFECT_OUTPUT ? &context : shown;
		}
		ASSERT_NE(shown, nullptr);
		ASSERT_EQ(shown->blocks.size(), 3u) << name;
		EXPECT_EQ(names(shown->node.id), "Orient: Face Camera Plane, Multiply Color Over Life, Multiply Size Over Life") << name;
		for (const GraphNode& block : std::span(shown->blocks).subspan(1)) {
			const GraphNodeDefinition* definition = definitions.Find(document, block);
			ASSERT_NE(definition, nullptr);
			EXPECT_TRUE(definition->name == "Multiply Color Over Life" || definition->name == "Multiply Size Over Life") << definition->name;
			ASSERT_EQ(block.inputs.size(), 1u);
			EXPECT_FALSE(block.inputs[0].value.empty()) << name << "'s " << definition->name;
		}
	}
}

TEST(EffectGraphEditOpsTest, SimpleTrailIsSpiralStrip) {
	GraphDocument document;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(Ops::TemplateDocument(3), document, error)) << error;
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(document, "New Effect.sleffect", effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	ASSERT_EQ(effect.systems.size(), 1u);
	const GraphEffectSystem& system = effect.systems[0];
	EXPECT_EQ(system.strips, 1u);
	EXPECT_EQ(system.capacity, 72u);
	EXPECT_TRUE(system.world);
	EXPECT_FALSE(system.boundsWorld);
	ASSERT_EQ(system.outputs.size(), 1u);
	EXPECT_TRUE(system.outputs[0].strip);
	ASSERT_EQ(effect.spawners.size(), 1u);
	ASSERT_EQ(effect.spawners[0].blocks.size(), 1u);
	EXPECT_EQ(effect.spawners[0].blocks[0].kind, GraphEffectSpawnBlock::Kind::ConstantRate);

	GraphDefinitions definitions;
	const auto names = [&](u32 id) {
		str out;
		for (const GraphNode& block : Context(document, id)->blocks) {
			const GraphNodeDefinition* definition = definitions.Find(document, block);
			out += (out.empty() ? "" : ", ") + (definition ? str(definition->name) : block.def);
		}
		return out;
	};
	EXPECT_EQ(names(system.initialize), "Set Lifetime, Add Position Sequential Circle, Set Velocity");
	EXPECT_EQ(names(system.update), "");
	u32 output = 0;
	for (const GraphContext& context : document.contexts) {
		output = context.node.def == GRAPH_EFFECT_OUTPUT_STRIP ? context.node.id : output;
	}
	ASSERT_NE(output, 0u);
	EXPECT_EQ(names(output), "Orient: Face Camera Position, Multiply Size Over Life, Multiply Color Over Life");
	const GraphNode& velocity = Context(document, system.initialize)->blocks.back();
	const auto port = std::find_if(velocity.inputs.begin(), velocity.inputs.end(), [](const GraphInput& input) { return input.port == "Velocity"; });
	ASSERT_NE(port, velocity.inputs.end());
	EXPECT_EQ(port->space, "World");
}

TEST(EffectGraphEditOpsTest, HeadAndTrailDrawsStripsFromEvents) {
	GraphDocument document;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(Ops::TemplateDocument(4), document, error)) << error;
	GraphEffect effect;
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphEffectTarget::Compile(document, "New Effect.sleffect", effect, diagnostics)) << (diagnostics.empty() ? str() : diagnostics[0].message);
	ASSERT_EQ(effect.systems.size(), 2u);
	const GraphEffectSystem& heads = effect.systems[0];
	const GraphEffectSystem& trails = effect.systems[1];
	EXPECT_EQ(heads.capacity, 13u);
	EXPECT_TRUE(heads.world);
	EXPECT_EQ(trails.strips, 16u);
	EXPECT_EQ(trails.capacity, 1600u);
	EXPECT_TRUE(trails.world);
	EXPECT_EQ(trails.parent, 0u);
	ASSERT_EQ(heads.updateEvents.size(), 1u);
	EXPECT_EQ(heads.updateEvents[0].child, 1u);
	ASSERT_EQ(effect.spawners.size(), 1u);
	ASSERT_EQ(heads.outputs.size(), 1u);
	ASSERT_EQ(trails.outputs.size(), 1u);
	EXPECT_TRUE(trails.outputs[0].strip);

	GraphDefinitions definitions;
	const auto names = [&](u32 id) {
		str out;
		for (const GraphNode& block : Context(document, id)->blocks) {
			const GraphNodeDefinition* definition = definitions.Find(document, block);
			out += (out.empty() ? "" : ", ") + (definition ? str(definition->name) : block.def);
		}
		return out;
	};
	EXPECT_EQ(names(heads.initialize), "Set Lifetime Random (Uniform), Set Size Random (Uniform), Set Color Random from Curve, Set Position Shape Sphere, "
									   "Set Velocity");
	EXPECT_EQ(names(heads.update), "Gravity, Linear Drag, Trigger Event Over Time");
	EXPECT_EQ(names(trails.initialize), "Set Color from Source, Set Position from Source, Set Alpha from Source, Set Size from Source, "
										"Set Lifetime from Source, Set Age from Source");
	u32 output = 0;
	for (const GraphContext& context : document.contexts) {
		output = context.node.def == GRAPH_EFFECT_OUTPUT_STRIP ? context.node.id : output;
	}
	ASSERT_NE(output, 0u);
	EXPECT_EQ(names(output), "Orient: Face Camera Position, Multiply Color Over Life, Multiply Size Custom");
	const auto source = [&](const GraphNode& node, std::string_view port) -> const GraphNode* {
		for (const GraphInput& input : node.inputs) {
			if (input.port == port) {
				return GraphDocumentOps::FindNode(document, input.fromNode);
			}
		}
		return nullptr;
	};
	const auto attribute = [&](const GraphNode* node) {
		str read;
		if (node && node->def == GRAPH_EFFECT_GET_ATTRIBUTE) {
			for (const GraphSetting& setting : node->settings) {
				read = setting.key == GRAPH_EFFECT_ATTRIBUTE_SETTING ? setting.value : read;
			}
		}
		return read;
	};
	const GraphNode* ratio = source(Context(document, output)->blocks.back(), "Sample Time");
	ASSERT_NE(ratio, nullptr);
	EXPECT_EQ(ratio->def, "Math/Divide");
	EXPECT_EQ(attribute(source(*ratio, "A")), "\"particleIndexInStrip\"");
	const GraphNode* places = source(*ratio, "B");
	ASSERT_NE(places, nullptr);
	EXPECT_EQ(places->def, "Math/Maximum");
	const GraphNode* less = source(*places, "A");
	ASSERT_NE(less, nullptr);
	EXPECT_EQ(less->def, "Math/Subtract");
	EXPECT_EQ(attribute(source(*less, "A")), "\"particleCountInStrip\"");
}

namespace {

	Vec3 Stored(const GraphDocument& document, u32 node, std::string_view port) {
		const GraphNode* held = GraphDocumentOps::FindNode(const_cast<GraphDocument&>(document), node);
		for (const GraphInput& input : held ? std::span<const GraphInput>(held->inputs) : std::span<const GraphInput>()) {
			if (input.port != port || input.value.size() < 2) {
				continue;
			}
			f32 values[3] = {};
			u32 read = 0;
			const char* at = input.value.c_str() + 1;
			for (char* end = nullptr; read < 3; at = end + 1) {
				values[read++] = std::strtof(at, &end);
				if (*end != ',') {
					break;
				}
			}
			if (read == 3) {
				return Vec3(values[0], values[1], values[2]);
			}
		}
		return Vec3(std::nanf(""), std::nanf(""), std::nanf(""));
	}

	u32 InitializeOf(const GraphDocument& document) {
		for (const GraphContext& context : document.contexts) {
			if (context.node.def == GRAPH_EFFECT_INITIALIZE) {
				return context.node.id;
			}
		}
		return 0;
	}

} // anonymous namespace

TEST(EffectGraphEditOpsTest, RecordingFillsUncomputedBox) {
	GraphDocument document;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(Ops::TemplateDocument(1), document, error)) << error;
	const u32 initialize = InitializeOf(document);
	ASSERT_NE(initialize, 0u);
	EXPECT_EQ(Ops::BoundsExclusionOf(document, initialize), Ops::BoundsExclusion::None);
	EXPECT_TRUE(Ops::BoundsExclusionLabel(Ops::BoundsExclusion::None).empty());
	ASSERT_TRUE(NodeShaderEditOps::SetNodeSetting(document, initialize, GRAPH_EFFECT_BOUNDS_SETTING_MODE, "\"Manual\""));
	EXPECT_EQ(Ops::BoundsExclusionOf(document, initialize), Ops::BoundsExclusion::Manual);
	EXPECT_EQ(Ops::BoundsExclusionLabel(Ops::BoundsExclusion::Manual), "(manual bounds)");
	ASSERT_TRUE(NodeShaderEditOps::SetNodeSetting(document, initialize, GRAPH_EFFECT_BOUNDS_SETTING_MODE, "\"Automatic\""));
	EXPECT_EQ(Ops::BoundsExclusionOf(document, initialize), Ops::BoundsExclusion::Automatic);
	EXPECT_EQ(Ops::BoundsExclusionLabel(Ops::BoundsExclusion::Automatic), "(automatic bounds)");
	ASSERT_TRUE(NodeShaderEditOps::SetNodeSetting(document, initialize, GRAPH_EFFECT_BOUNDS_SETTING_MODE, "\"Recorded\""));
	const u32 vector = NodeShaderEditOps::AddNode(document, "Input/Vector3", Vec2(-400.0f, 200.0f));
	ASSERT_TRUE(GraphDocumentOps::Connect(document, vector, "Out", initialize, GRAPH_EFFECT_BOUNDS_SIZE));
	EXPECT_EQ(Ops::BoundsExclusionOf(document, initialize), Ops::BoundsExclusion::GraphComputed);
	EXPECT_EQ(Ops::BoundsExclusionLabel(Ops::BoundsExclusion::GraphComputed), "(bounds from the graph)");

	EXPECT_TRUE(Ops::BoundsExclusionTooltip(Ops::BoundsExclusion::None).empty());
	EXPECT_EQ(Ops::BoundsExclusionTooltip(Ops::BoundsExclusion::Manual),
			  "The recording leaves this system out (its Bounds Setting Mode is not Recorded).");
	EXPECT_EQ(Ops::BoundsExclusionTooltip(Ops::BoundsExclusion::Automatic), Ops::BoundsExclusionTooltip(Ops::BoundsExclusion::Manual));
	EXPECT_EQ(Ops::BoundsExclusionTooltip(Ops::BoundsExclusion::GraphComputed),
			  "The recording leaves this system out (operators set its bounds).");
}

TEST(EffectGraphEditOpsTest, RecordedBoxUsesCenterSpace) {
	const Mat4 moved = Mat4::Translation(Vec3(10.0f, 0.0f, 0.0f));
	const AABB box(Vec3(-1.0f, 0.0f, -2.0f), Vec3(3.0f, 4.0f, 2.0f));
	const auto applied = [&](u32 index, bool worldCenter, Vec3 center) {
		SCOPED_TRACE(index);
		GraphDocument document;
		str error;
		ASSERT_TRUE(GraphDocumentOps::Read(Ops::TemplateDocument(index), document, error)) << error;
		const u32 initialize = InitializeOf(document);
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		ASSERT_TRUE(GraphEffectTarget::Compile(document, "New Effect.sleffect", effect, diagnostics));
		const bool world = effect.systems[0].world;
		if (worldCenter) {
			ASSERT_TRUE(GraphDocumentOps::SetInputSpace(document, initialize, GRAPH_EFFECT_BOUNDS_CENTER, GRAPH_SPACE_WORLD));
		}
		ASSERT_TRUE(Ops::ApplyRecordedBounds(document, initialize, box, world, moved));
		EXPECT_EQ(Stored(document, initialize, GRAPH_EFFECT_BOUNDS_CENTER), center);
		EXPECT_EQ(Stored(document, initialize, GRAPH_EFFECT_BOUNDS_SIZE), Vec3(4.0f, 4.0f, 4.0f));
	};
	applied(1, false, Vec3(1.0f, 2.0f, 0.0f));
	applied(1, true, Vec3(11.0f, 2.0f, 0.0f));
	applied(3, false, Vec3(-9.0f, 2.0f, 0.0f));
	applied(3, true, Vec3(1.0f, 2.0f, 0.0f));

	GraphDocument document;
	str error;
	ASSERT_TRUE(GraphDocumentOps::Read(Ops::TemplateDocument(1), document, error)) << error;
	const u32 initialize = InitializeOf(document);
	const str before = GraphDocumentOps::Write(document);
	EXPECT_FALSE(Ops::ApplyRecordedBounds(document, initialize, AABB::Empty(), false, moved));
	ASSERT_TRUE(NodeShaderEditOps::SetNodeSetting(document, initialize, GRAPH_EFFECT_BOUNDS_SETTING_MODE, "\"Manual\""));
	const str manual = GraphDocumentOps::Write(document);
	EXPECT_FALSE(Ops::ApplyRecordedBounds(document, initialize, box, false, moved));
	EXPECT_EQ(GraphDocumentOps::Write(document), manual);
	EXPECT_NE(before, manual);
}
