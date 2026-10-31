#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"

#include "GraphProperties.h"
#include "GraphSurface.h"
#include "NodeShaderEditOps.h"
#include "NodeShaderStackOps.h"

#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>

using namespace Shalltear;
using namespace Shalltear::NodeShaderTest;

namespace {

	namespace Ops = NodeShaderEditOps;

	namespace Stack = NodeShaderStackOps;

	struct StackBuilder : DocumentBuilder {
		u32 fragment = 0;

		StackBuilder() {
			GraphContext context;
			context.node.id = 900;
			context.node.def = "Context/Fragment";
			fragment = context.node.id;
			document.contexts.push_back(std::move(context));
			document.nextId = 901;
		}
	};

	const GraphInput* InputOf(GraphDocument& document, u32 node, std::string_view port) {
		const GraphNode* held = GraphDocumentOps::FindNode(document, node);
		if (!held) {
			return nullptr;
		}
		for (const GraphInput& input : held->inputs) {
			if (input.port == port) {
				return &input;
			}
		}
		return nullptr;
	}

	const GraphProperty* PropertyNamed(const GraphDocument& document, std::string_view name) {
		for (const GraphProperty& property : document.properties) {
			if (property.name == name) {
				return &property;
			}
		}
		return nullptr;
	}

} // namespace

TEST(NodeShaderEditOpsTest, MasterStackIsNotNodeToAdd) {
	StackBuilder builder;
	EXPECT_EQ(Ops::AddNode(builder.document, "Context/Fragment", {}), 0u);
	EXPECT_EQ(Ops::AddNode(builder.document, "Block/Alpha", {}), 0u);
	EXPECT_EQ(Ops::AddNode(builder.document, "Math/NoSuchNode", {}), 0u);
	const u32 add = Ops::AddNode(builder.document, "Math/Add", {120.4f, -30.6f});
	ASSERT_NE(add, 0u);
	const GraphNode* node = GraphDocumentOps::FindNode(builder.document, add);
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(node->x, 120);
	EXPECT_EQ(node->y, -31);
}

TEST(NodeShaderEditOpsTest, WireMeetsCompatiblePort) {
	StackBuilder builder;
	builder.Node(10, "Channel/Combine");
	builder.Node(11, "Math/Add");
	builder.Node(12, "Input/SampleTexture2D");
	builder.Property(20, 21, "albedo", "Texture2D");
	builder.Property(22, 23, "useTint", "Boolean");

	str why;
	EXPECT_TRUE(Ops::CanConnect(builder.document, 10, "RGBA", 11, "A", &why)) << why;
	EXPECT_TRUE(Ops::CanConnect(builder.document, 21, "Out", 12, "Texture", &why)) << why;
	EXPECT_FALSE(Ops::CanConnect(builder.document, 10, "RGBA", 12, "Texture", &why));
	EXPECT_NE(why.find("texture"), str::npos) << why;
	EXPECT_FALSE(Ops::CanConnect(builder.document, 21, "Out", 11, "A"));
	EXPECT_TRUE(Ops::CanConnect(builder.document, 23, "Out", 11, "B"));
	EXPECT_FALSE(Ops::CanConnect(builder.document, 10, "RGBA", 10, "R"));
}

TEST(NodeShaderEditOpsTest, ARedirectCarriesWhatFeedsIt) {
	StackBuilder builder;
	builder.Property(20, 21, "albedo", "Texture2D");
	builder.Node(30, GraphDocumentOps::REDIRECT_DEF);
	builder.Connect(21, "Out", 30, GraphDocumentOps::REDIRECT_IN);
	builder.Node(12, "Input/SampleTexture2D");
	builder.Node(11, "Math/Add");

	EXPECT_TRUE(Ops::CanConnect(builder.document, 30, GraphDocumentOps::REDIRECT_OUT, 12, "Texture"));
	EXPECT_FALSE(Ops::CanConnect(builder.document, 30, GraphDocumentOps::REDIRECT_OUT, 11, "A"));
}

TEST(NodeShaderEditOpsTest, ConnectionStateNeedsCustomBinding) {
	StackBuilder builder;
	builder.Property(20, 21, "bound", "Float");
	builder.document.properties.back().settings.push_back({str("customBinding"), str("true")});
	builder.Property(22, 23, "plain", "Float");
	builder.Node(10, "Channel/Combine");
	builder.Node(11, "Utility/BranchOnInputConnection");
	builder.Node(30, GraphDocumentOps::REDIRECT_DEF);
	builder.Connect(21, "Out", 30, GraphDocumentOps::REDIRECT_IN);

	str why;
	EXPECT_TRUE(Ops::CanConnect(builder.document, 21, "Out", 11, "Input", &why)) << why;
	EXPECT_TRUE(Ops::CanConnect(builder.document, 30, GraphDocumentOps::REDIRECT_OUT, 11, "Input", &why)) << why;
	EXPECT_FALSE(Ops::CanConnect(builder.document, 23, "Out", 11, "Input", &why));
	EXPECT_NE(why.find("Use Custom Binding"), str::npos) << why;
	EXPECT_FALSE(Ops::CanConnect(builder.document, 10, "RGBA", 11, "Input"));
	EXPECT_TRUE(Ops::CanConnect(builder.document, 23, "Out", 11, "Connected"));

	EXPECT_TRUE(Ops::PropertyFeeds(builder.document, 20, 11, "Input"));
	EXPECT_FALSE(Ops::PropertyFeeds(builder.document, 22, 11, "Input"));
	EXPECT_TRUE(Ops::PropertyFeeds(builder.document, 22, 11, "Connected"));
}

TEST(NodeShaderEditOpsTest, WireThatClosesLoopIsRefused) {
	StackBuilder builder;
	builder.Node(1, "Math/OneMinus");
	builder.Node(2, "Math/OneMinus");
	builder.Node(3, "Math/OneMinus");
	builder.Connect(1, "Out", 2, "In");
	builder.Connect(2, "Out", 3, "In");
	str why;
	EXPECT_FALSE(Ops::CanConnect(builder.document, 3, "Out", 1, "In", &why));
	EXPECT_NE(why.find("cycle"), str::npos) << why;
	EXPECT_TRUE(Ops::CanConnect(builder.document, 1, "Out", 3, "In"));
}

TEST(NodeShaderEditOpsTest, ConstantIsStoredAsCompilerReadsIt) {
	StackBuilder builder;
	builder.Node(1, "Math/Add");
	const f32 three[] = {1.0f, 0.5f, -2.0f};
	ASSERT_TRUE(Ops::SetInputConstant(builder.document, 1, "B", three));
	const f32 one[] = {0.25f};
	ASSERT_TRUE(Ops::SetInputConstant(builder.document, 1, "A", one));
	const GraphInput* a = InputOf(builder.document, 1, "A");
	const GraphInput* b = InputOf(builder.document, 1, "B");
	ASSERT_NE(a, nullptr);
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(a->value, "0.25");
	EXPECT_EQ(b->value, "[1,0.5,-2]");
	const GraphNode* node = GraphDocumentOps::FindNode(builder.document, 1);
	ASSERT_EQ(node->inputs.size(), 2u);
	EXPECT_EQ(node->inputs[0].port, "A");

	Compiled compiled = Compile(builder.document, {{1, "Out"}});
	ASSERT_TRUE(compiled.ok) << compiled.Report();
}

TEST(NodeShaderEditOpsTest, AQuotedTextReadsBackAsItself) {
	const std::string_view texts[] = {"plain", "two\nlines", "a \"quote\" and a \\ backslash", "a\ttab\r\n", std::string_view("\x01", 1)};
	for (std::string_view text : texts) {
		const str json = Ops::Quote(text);
		LinearAllocator arena(json.size() + 64, MemTag::Editor);
		const JSONValue value = JSONParse(json, arena);
		ASSERT_TRUE(value.IsString()) << json;
		EXPECT_EQ(value.AsString(), text) << json;
	}
}

TEST(NodeShaderEditOpsTest, DeleteLeavesTheMasterStack) {
	StackBuilder builder;
	const u32 alpha = Stack::AddBlock(builder.document, builder.fragment, "Block/Alpha");
	builder.Node(5, "Math/Add");
	const u32 ids[] = {builder.fragment, alpha, 5};
	EXPECT_EQ(Ops::Delete(builder.document, ids), 1u);
	EXPECT_EQ(builder.document.contexts.size(), 1u);
	EXPECT_EQ(builder.document.contexts[0].blocks.size(), 1u);
	EXPECT_TRUE(builder.document.nodes.empty());
}

TEST(NodeShaderEditOpsTest, TheStackOrderIsTheSurfacesBlocks) {
	StackBuilder builder;
	Vector<std::string_view> missing{GetMemoryResource(MemTag::Editor)};
	Stack::MissingBlocks(builder.document, builder.fragment, missing);
	Vector<std::string_view> surface{GetMemoryResource(MemTag::Editor)};
	for (std::string_view def : GraphSurface::FragmentBlocks()) {
		surface.push_back(def);
	}
	std::sort(missing.begin(), missing.end());
	std::sort(surface.begin(), surface.end());
	EXPECT_EQ(missing, surface);
}

TEST(NodeShaderEditOpsTest, BlockAddedBackLandsInOrder) {
	StackBuilder builder;
	const u32 alpha = Stack::AddBlock(builder.document, builder.fragment, "Block/Alpha");
	const u32 base = Stack::AddBlock(builder.document, builder.fragment, "Block/BaseColor");
	const u32 metallic = Stack::AddBlock(builder.document, builder.fragment, "Block/Metallic");
	ASSERT_NE(alpha, 0u);
	EXPECT_EQ(Stack::AddBlock(builder.document, builder.fragment, "Block/Alpha"), 0u);
	const GraphContext& stack = builder.document.contexts[0];
	ASSERT_EQ(stack.blocks.size(), 3u);
	EXPECT_EQ(stack.blocks[0].id, base);
	EXPECT_EQ(stack.blocks[1].id, metallic);
	EXPECT_EQ(stack.blocks[2].id, alpha);

	EXPECT_TRUE(Stack::RemoveBlock(builder.document, metallic));
	EXPECT_EQ(builder.document.contexts[0].blocks.size(), 2u);
	Vector<std::string_view> missing{GetMemoryResource(MemTag::Editor)};
	Stack::MissingBlocks(builder.document, builder.fragment, missing);
	EXPECT_NE(std::find(missing.begin(), missing.end(), "Block/Metallic"), missing.end());
}

TEST(NodeShaderEditOpsTest, UnlitReadsColorAndAlpha) {
	StackBuilder builder;
	EXPECT_TRUE(Stack::IsBlockActive(builder.document, "Block/Metallic"));
	ASSERT_TRUE(Ops::SetGraphSetting(builder.document, "target", "\"unlit\""));
	EXPECT_EQ(Ops::GraphSettingText(builder.document, "target"), "unlit");
	EXPECT_FALSE(Stack::IsBlockActive(builder.document, "Block/Metallic"));
	EXPECT_TRUE(Stack::IsBlockActive(builder.document, "Block/BaseColor"));
	EXPECT_TRUE(Stack::IsBlockActive(builder.document, "Block/Alpha"));
}

TEST(NodeShaderEditOpsTest, NewPropertyNamedByType) {
	StackBuilder builder;
	const u32 first = Ops::AddProperty(builder.document, "Float");
	const u32 second = Ops::AddProperty(builder.document, "Float");
	EXPECT_EQ(Ops::AddProperty(builder.document, "Matrix4"), 0u);
	ASSERT_NE(first, 0u);
	ASSERT_NE(second, 0u);
	ASSERT_EQ(builder.document.properties.size(), 2u);
	EXPECT_EQ(builder.document.properties[0].name, "Float");
	EXPECT_EQ(builder.document.properties[0].reference, "_Float");
	EXPECT_EQ(builder.document.properties[1].name, "Float (1)");
	EXPECT_EQ(builder.document.properties[1].reference, "_Float1");

	Vector<GraphPropertyInfo> parsed{GetMemoryResource(MemTag::Editor)};
	Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
	for (std::string_view type : Ops::PropertyTypes()) {
		Ops::AddProperty(builder.document, type);
	}
	EXPECT_TRUE(GraphProperties::ParseBlackboard(builder.document, parsed, diagnostics))
		<< (diagnostics.empty() ? str() : diagnostics[0].message);
}

TEST(NodeShaderEditOpsTest, ReferenceFollowsNameUntilTyped) {
	StackBuilder builder;
	const u32 id = Ops::AddProperty(builder.document, "Float");
	ASSERT_TRUE(Ops::RenameProperty(builder.document, id, "Dissolve Amount"));
	EXPECT_EQ(builder.document.properties[0].reference, "_DissolveAmount");

	ASSERT_TRUE(Ops::SetPropertyReference(builder.document, id, "cutoff"));
	ASSERT_TRUE(Ops::RenameProperty(builder.document, id, "Threshold"));
	EXPECT_EQ(builder.document.properties[0].name, "Threshold");
	EXPECT_EQ(builder.document.properties[0].reference, "cutoff");
}

TEST(NodeShaderEditOpsTest, DeletingPropertyTakesNodesReadingIt) {
	StackBuilder builder;
	const u32 property = Ops::AddProperty(builder.document, "Color");
	const u32 reader = Ops::AddPropertyNode(builder.document, property, {10.0f, 10.0f});
	ASSERT_NE(reader, 0u);
	EXPECT_EQ(Ops::PropertyOf(builder.document, reader), property);
	builder.Node(50, "Math/OneMinus");
	builder.Connect(reader, "Out", 50, "In");

	const u32 doomed[] = {property};
	EXPECT_EQ(Ops::DeleteProperties(builder.document, doomed), 1u);
	EXPECT_TRUE(builder.document.properties.empty());
	EXPECT_EQ(GraphDocumentOps::FindNode(builder.document, reader), nullptr);
	ASSERT_NE(GraphDocumentOps::FindNode(builder.document, 50), nullptr);
	EXPECT_EQ(InputOf(builder.document, 50, "In"), nullptr);
}

TEST(NodeShaderEditOpsTest, MovingAPropertyReordersTheBlackboard) {
	StackBuilder builder;
	const u32 a = Ops::AddProperty(builder.document, "Float");
	const u32 b = Ops::AddProperty(builder.document, "Color");
	ASSERT_TRUE(Ops::MoveProperty(builder.document, b, 0));
	EXPECT_EQ(builder.document.properties[0].id, b);
	EXPECT_EQ(builder.document.properties[1].id, a);
	EXPECT_FALSE(Ops::MoveProperty(builder.document, b, 0));
}

TEST(NodeShaderEditOpsTest, PastedNodeBringsProperty) {
	StackBuilder source;
	const u32 tint = Ops::AddProperty(source.document, "Color");
	Ops::RenameProperty(source.document, tint, "Tint");
	const u32 reader = Ops::AddPropertyNode(source.document, tint, {0.0f, 0.0f});
	source.Node(40, "Math/OneMinus");
	source.Connect(reader, "Out", 40, "In");
	const u32 copied[] = {reader, 40, source.fragment};
	const str clip = Ops::Copy(source.document, copied);
	ASSERT_FALSE(clip.empty());

	StackBuilder target;
	Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(Ops::Paste(target.document, clip, {50.0f, 0.0f}, pasted, error)) << error;
	EXPECT_EQ(target.document.contexts.size(), 1u);
	ASSERT_EQ(target.document.properties.size(), 1u);
	const GraphProperty* brought = PropertyNamed(target.document, "Tint");
	ASSERT_NE(brought, nullptr);
	u32 pastedReader = 0;
	for (u32 id : pasted) {
		pastedReader = Ops::PropertyOf(target.document, id) != 0 ? id : pastedReader;
	}
	ASSERT_NE(pastedReader, 0u);
	EXPECT_EQ(Ops::PropertyOf(target.document, pastedReader), brought->id);

	ASSERT_TRUE(Ops::Paste(target.document, clip, {100.0f, 0.0f}, pasted, error)) << error;
	EXPECT_EQ(target.document.properties.size(), 1u);
}

TEST(NodeShaderEditOpsTest, PasteIntoSameGraphReadsSameProperty) {
	StackBuilder builder;
	const u32 speed = Ops::AddProperty(builder.document, "Float");
	const u32 reader = Ops::AddPropertyNode(builder.document, speed, {0.0f, 0.0f});
	const u32 copied[] = {reader};
	const str clip = Ops::Copy(builder.document, copied);
	Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(Ops::Paste(builder.document, clip, {30.0f, 30.0f}, pasted, error)) << error;
	EXPECT_EQ(builder.document.properties.size(), 1u);
	ASSERT_EQ(pasted.size(), 1u);
	EXPECT_EQ(Ops::PropertyOf(builder.document, pasted[0]), speed);
}

TEST(NodeShaderEditOpsTest, HiddenPortDropsWire) {
	StackBuilder builder;
	builder.Property(20, 21, "albedo", "Texture2D");
	GraphNode& sample = builder.Node(1, "Input/SampleTexture2D");
	DocumentBuilder::Setting(sample, "Mip Sampling Mode", "\"LOD\"");
	DocumentBuilder::Constant(sample, "LOD", "3");
	builder.Node(2, "Math/OneMinus");
	builder.Connect(21, "Out", 1, "Texture");
	builder.Connect(2, "Out", 1, "LOD");
	ASSERT_TRUE(Ops::SetNodeSetting(builder.document, 1, "Mip Sampling Mode", "\"Standard\""));
	const GraphInput* lod = InputOf(builder.document, 1, "LOD");
	ASSERT_NE(lod, nullptr);
	EXPECT_EQ(lod->fromNode, 0u);
	EXPECT_EQ(lod->value, "3");

	builder.Node(3, "Input/SampleTexture2D");
	builder.Connect(3, "R", 1, "LOD");
	Compiled standard = Compile(builder.document, {{1, "RGBA"}}, GraphStageFragment, TestBindings());
	EXPECT_TRUE(standard.ok) << standard.Report();
	ASSERT_TRUE(Ops::SetNodeSetting(builder.document, 1, "Mip Sampling Mode", "\"LOD\""));
	builder.Connect(3, "R", 1, "LOD");
	Compiled shown = Compile(builder.document, {{1, "RGBA"}}, GraphStageFragment, TestBindings());
	EXPECT_FALSE(shown.ok);
	EXPECT_TRUE(shown.Names(3, "Texture"));
}
