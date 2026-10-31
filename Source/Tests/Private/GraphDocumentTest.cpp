#include "TestsPCH.h"
#include "Shalltear/Editor/GraphDocument.h"

using namespace Shalltear;

namespace {

	constexpr std::string_view Canonical = R"({
	"_type": "NodeShader",
	"version": 1,
	"nextId": 13,
	"settings": {"target":"lit"},
	"properties": [
		{"id":1,"name":"Tint","reference":"tint","type":"Color","default":[1,1,1,1],"settings":{"group":"Surface Inputs","mode":"hdr"}},
		{"id":12,"name":"Albedo","reference":"albedo","type":"Texture2D"}
	],
	"nodes": [
		{"id":4,"def":"Input/Property","x":-320,"y":40,"group":9,"settings":{"property":1}},
		{"id":5,"def":"Math/Multiply","x":-120,"y":60,"group":9,"in":{"A":{"from":[4,"Out"]},"B":{"from":[4,"Out"],"value":[0.5]}}},
		{"id":7,"def":"Math/Sine","x":0,"y":200,"in":{"In":{"value":[0.25]}}}
	],
	"contexts": [
		{"id":2,"def":"Context/Fragment","x":200,"y":0,"blocks":[
			{"id":6,"def":"Block/BaseColor","in":{"Value":{"from":[5,"Out"]}}},
			{"id":8,"def":"Block/Alpha"}
		]},
		{"id":3,"def":"Context/Vertex","x":200,"y":-200,"blocks":[]}
	],
	"groups": [
		{"id":9,"title":"Tint","x":0,"y":0}
	],
	"notes": [
		{"id":10,"x":-320,"y":-160,"w":240,"h":120,"title":"Why","body":"the tint goes first\nso a material can override it","theme":"dark","size":"large","group":9}
	]
}
)";

	GraphDocument ReadOrFail(std::string_view text) {
		GraphDocument document;
		str error;
		EXPECT_TRUE(GraphDocumentOps::Read(text, document, error)) << error;
		return document;
	}

	const GraphInput* InputOf(const GraphNode& node, std::string_view port) {
		for (const GraphInput& input : node.inputs) {
			if (input.port == port) {
				return &input;
			}
		}
		return nullptr;
	}

	constexpr std::string_view Chain = R"({
	"_type": "NodeShader",
	"version": 1,
	"nextId": 4,
	"settings": {},
	"properties": [],
	"nodes": [
		{"id":1,"def":"Input/Time","x":0,"y":0},
		{"id":2,"def":"Math/Sine","x":200,"y":0,"in":{"In":{"from":[1,"Out"]}}},
		{"id":3,"def":"Math/Multiply","x":400,"y":0,"in":{"A":{"from":[2,"Out"]},"B":{"value":[2]}}}
	],
	"contexts": [],
	"groups": [],
	"notes": []
}
)";

} // anonymous namespace

TEST(GraphDocumentTest, UnchangedDocumentReExportsByteForByte) {
	const GraphDocument document = ReadOrFail(Canonical);
	ASSERT_EQ(document.nodes.size(), 3u);
	ASSERT_EQ(document.contexts.size(), 2u);
	ASSERT_EQ(document.contexts[0].blocks.size(), 2u);
	EXPECT_EQ(GraphDocumentOps::Write(document), Canonical);

	const GraphInput* both = InputOf(document.nodes[1], "B");
	ASSERT_NE(both, nullptr);
	EXPECT_EQ(both->fromNode, 4u);
	EXPECT_EQ(both->value, "[0.5]");
}

TEST(GraphDocumentTest, HandWrittenDocumentSettlesOnFirstWrite) {
	constexpr std::string_view Messy = R"({ "nodes": [ { "y": 40.4, "x": -12.6, "def": "Math/Add", "id": 3,
		"in": { "B": { "value": [ 1.5 ] }, "A": { "from": [ 2, "Out" ] }, "C": {} },
		"settings": { "zeta": true, "alpha": "x", "alpha": "y" } },
		{ "id": 2, "def": "Input/Time" } ],
	"version": 1, "_type": "NodeShader" })";
	const GraphDocument messy = ReadOrFail(Messy);
	const str first = GraphDocumentOps::Write(messy);
	const str second = GraphDocumentOps::Write(ReadOrFail(first));
	EXPECT_EQ(first, second);

	ASSERT_EQ(messy.nodes.size(), 2u);
	const GraphNode& add = messy.nodes[0];
	EXPECT_EQ(add.x, -13);
	EXPECT_EQ(add.y, 40);
	ASSERT_EQ(add.settings.size(), 2u);
	EXPECT_EQ(add.settings[0].key, "alpha");
	EXPECT_EQ(add.settings[0].value, "\"y\"");
	ASSERT_EQ(add.inputs.size(), 2u);
	EXPECT_EQ(add.inputs[0].port, "A");
	EXPECT_EQ(messy.nextId, 4u);
	EXPECT_NE(first.find("{\"id\":3,\"def\":\"Math/Add\",\"x\":-13,\"y\":40,\"settings\":"), str::npos);
}

TEST(GraphDocumentTest, InputHasOneSource) {
	GraphDocument document = ReadOrFail(Chain);
	ASSERT_TRUE(GraphDocumentOps::Connect(document, 1, "Out", 3, "A"));
	const GraphNode* multiply = GraphDocumentOps::FindNode(document, 3);
	ASSERT_NE(multiply, nullptr);
	u32 feeding = 0;
	for (const GraphInput& input : multiply->inputs) {
		feeding += input.port == "A" && input.fromNode != 0 ? 1u : 0u;
	}
	EXPECT_EQ(feeding, 1u);
	EXPECT_EQ(InputOf(*multiply, "A")->fromNode, 1u);

	EXPECT_FALSE(GraphDocumentOps::Connect(document, 3, "Out", 3, "B"));
	EXPECT_FALSE(GraphDocumentOps::Connect(document, 99, "Out", 3, "B"));
	EXPECT_FALSE(GraphDocumentOps::Connect(document, 1, "Out", 99, "B"));

	ASSERT_TRUE(GraphDocumentOps::Connect(document, 1, "Out", 3, "B"));
	ASSERT_TRUE(GraphDocumentOps::Disconnect(document, 3, "B"));
	multiply = GraphDocumentOps::FindNode(document, 3);
	ASSERT_NE(InputOf(*multiply, "B"), nullptr);
	EXPECT_EQ(InputOf(*multiply, "B")->fromNode, 0u);
	EXPECT_EQ(InputOf(*multiply, "B")->value, "[2]");
	ASSERT_TRUE(GraphDocumentOps::Disconnect(document, 3, "A"));
	EXPECT_EQ(InputOf(*GraphDocumentOps::FindNode(document, 3), "A"), nullptr);
	EXPECT_FALSE(GraphDocumentOps::Disconnect(document, 3, "A"));
}

TEST(GraphDocumentTest, PasteMintsIdsKeepsInnerEdges) {
	GraphDocument document = ReadOrFail(Chain);
	const u32 middle[] = {2, 3};
	const str fragment = GraphDocumentOps::Copy(document, middle);
	ASSERT_FALSE(fragment.empty());

	const GraphDocument copied = ReadOrFail(fragment);
	ASSERT_EQ(copied.nodes.size(), 2u);
	EXPECT_TRUE(copied.nodes[0].inputs.empty());
	ASSERT_NE(InputOf(copied.nodes[1], "A"), nullptr);
	EXPECT_EQ(InputOf(copied.nodes[1], "A")->fromNode, 2u);

	Vector<u32> first{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(GraphDocumentOps::Paste(document, fragment, Vec2(10.0f, 300.0f), first, error)) << error;
	ASSERT_EQ(first.size(), 2u);
	EXPECT_EQ(first[0], 4u);
	EXPECT_EQ(first[1], 5u);
	EXPECT_EQ(document.nextId, 6u);

	const GraphNode* sine = GraphDocumentOps::FindNode(document, first[0]);
	const GraphNode* multiply = GraphDocumentOps::FindNode(document, first[1]);
	ASSERT_NE(sine, nullptr);
	ASSERT_NE(multiply, nullptr);
	EXPECT_EQ(sine->x, 210);
	EXPECT_EQ(sine->y, 300);
	EXPECT_EQ(InputOf(*sine, "In"), nullptr);
	ASSERT_NE(InputOf(*multiply, "A"), nullptr);
	EXPECT_EQ(InputOf(*multiply, "A")->fromNode, first[0]);
	EXPECT_EQ(InputOf(*multiply, "B")->value, "[2]");

	Vector<u32> second{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(GraphDocumentOps::Paste(document, fragment, Vec2(0.0f, 0.0f), second, error)) << error;
	ASSERT_EQ(second.size(), 2u);
	EXPECT_EQ(second[0], 6u);
	EXPECT_EQ(InputOf(*GraphDocumentOps::FindNode(document, second[1]), "A")->fromNode, second[0]);

	Vector<u32> duplicated{GetMemoryResource(MemTag::Editor)};
	const u32 one[] = {1};
	ASSERT_TRUE(GraphDocumentOps::Duplicate(document, one, Vec2(0.0f, 80.0f), duplicated));
	ASSERT_EQ(duplicated.size(), 1u);
	EXPECT_EQ(GraphDocumentOps::FindNode(document, duplicated[0])->y, 80);
	EXPECT_EQ(document.nodes.size(), 8u);
}

TEST(GraphDocumentTest, PasteRefusesForeignOrEmpty) {
	GraphDocument document = ReadOrFail(Chain);
	const u32 all[] = {1, 2, 3};
	str fragment = GraphDocumentOps::Copy(document, all);
	const size_t at = fragment.find("NodeShader");
	ASSERT_NE(at, str::npos);
	fragment.replace(at, 11, "EffectGraph");

	Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
	str error;
	const u32 before = document.nextId;
	EXPECT_FALSE(GraphDocumentOps::Paste(document, fragment, Vec2(), pasted, error));
	EXPECT_FALSE(error.empty());
	EXPECT_EQ(document.nextId, before);
	EXPECT_EQ(document.nodes.size(), 3u);

	const u32 none[] = {42};
	EXPECT_TRUE(GraphDocumentOps::Copy(document, none).empty());
}

TEST(GraphDocumentTest, DeleteCutsEdgesKeepsConstants) {
	GraphDocument document = ReadOrFail(Chain);
	const u32 sine[] = {2};
	EXPECT_EQ(GraphDocumentOps::Delete(document, sine), 1u);
	EXPECT_EQ(GraphDocumentOps::FindNode(document, 2), nullptr);
	const GraphNode* multiply = GraphDocumentOps::FindNode(document, 3);
	ASSERT_NE(multiply, nullptr);
	EXPECT_EQ(InputOf(*multiply, "A"), nullptr);
	ASSERT_NE(InputOf(*multiply, "B"), nullptr);
	EXPECT_EQ(document.nextId, 4u);

	GraphDocument canonical = ReadOrFail(Canonical);
	ASSERT_TRUE(GraphDocumentOps::Connect(canonical, 6, "Out", 7, "In"));
	const u32 fragmentContext[] = {2};
	EXPECT_EQ(GraphDocumentOps::Delete(canonical, fragmentContext), 3u);
	EXPECT_EQ(GraphDocumentOps::FindNode(canonical, 6), nullptr);
	ASSERT_NE(InputOf(*GraphDocumentOps::FindNode(canonical, 7), "In"), nullptr);
	EXPECT_EQ(InputOf(*GraphDocumentOps::FindNode(canonical, 7), "In")->fromNode, 0u);

	const u32 missing[] = {99};
	EXPECT_EQ(GraphDocumentOps::Delete(canonical, missing), 0u);
}

TEST(GraphDocumentTest, UnfaithfulRewriteRefused) {
	GraphDocument kept = ReadOrFail(Chain);
	const str before = GraphDocumentOps::Write(kept);
	const str newer = std::format(R"({{"_type":"NodeShader","version":{},"nodes":[]}})", GraphDocumentOps::FORMAT_VERSION + 1);
	const std::string_view refused[] = {
		"[1, 2]",
		R"({"version":1,"nodes":[]})",
		R"({"_type":"NodeShader","nodes":[]})",
		newer,
		R"({"_type":"NodeShader","version":1,"nodes":[{"id":1,"def":"A"},{"id":1,"def":"B"}]})",
		R"({"_type":"NodeShader","version":1,"nodes":[{"id":1,"def":"A"}],"groups":[{"id":1}]})",
		R"({"_type":"NodeShader","version":1,"nodes":[{"id":1}]})",
		R"({"_type":"NodeShader","version":1,"nodes":[{"id":0.5,"def":"A"}]})",
	};
	for (std::string_view text : refused) {
		str error;
		EXPECT_FALSE(GraphDocumentOps::Read(text, kept, error)) << "accepted: " << text;
		EXPECT_FALSE(error.empty()) << text;
		EXPECT_EQ(GraphDocumentOps::Write(kept), before) << text;
	}
}

TEST(GraphDocumentTest, CounterNeverFallsBelowIdFileHolds) {
	const GraphDocument document = ReadOrFail(
		R"({"_type":"NodeShader","version":1,"nextId":2,"nodes":[{"id":7,"def":"A"}],"groups":[{"id":9}]})");
	EXPECT_EQ(document.nextId, 10u);
	const GraphDocument ahead = ReadOrFail(R"({"_type":"NodeShader","version":1,"nextId":40,"nodes":[{"id":7,"def":"A"}]})");
	EXPECT_EQ(ahead.nextId, 40u);
}

TEST(GraphDocumentTest, MoveSnapsBlockStaysInStack) {
	GraphDocument document = ReadOrFail(Canonical);
	ASSERT_TRUE(GraphDocumentOps::MoveNode(document, 4, Vec2(10.6f, -3.4f)));
	EXPECT_EQ(GraphDocumentOps::FindNode(document, 4)->x, 11);
	EXPECT_EQ(GraphDocumentOps::FindNode(document, 4)->y, -3);
	ASSERT_TRUE(GraphDocumentOps::MoveNode(document, 2, Vec2(-50.0f, 0.0f)));
	EXPECT_EQ(document.contexts[0].node.x, -50);
	EXPECT_FALSE(GraphDocumentOps::MoveNode(document, 6, Vec2(1.0f, 1.0f)));
	EXPECT_FALSE(GraphDocumentOps::MoveNode(document, 99, Vec2()));
}

namespace {

	const GraphNode* NodeById(const GraphDocument& document, u32 id) {
		for (const GraphNode& node : document.nodes) {
			if (node.id == id) {
				return &node;
			}
		}
		return nullptr;
	}

} // anonymous namespace

TEST(GraphDocumentTest, DeletingGroupUngroups) {
	GraphDocument document = ReadOrFail(Chain);
	const u32 members[] = {1, 2};
	const u32 group = GraphDocumentOps::AddGroup(document, "Wave", members, Vec2(10.0f, 10.0f));
	ASSERT_NE(group, 0u);
	EXPECT_EQ(NodeById(document, 1)->group, group);
	EXPECT_EQ(NodeById(document, 2)->group, group);
	EXPECT_EQ(NodeById(document, 3)->group, 0u);
	const str written = GraphDocumentOps::Write(document);
	EXPECT_NE(written.find("{\"id\":1,\"def\":\"Input/Time\",\"x\":0,\"y\":0,\"group\":"), str::npos);

	const u32 doomed[] = {group};
	EXPECT_EQ(GraphDocumentOps::Delete(document, doomed), 1u);
	EXPECT_TRUE(document.groups.empty());
	ASSERT_EQ(document.nodes.size(), 3u);
	EXPECT_EQ(NodeById(document, 1)->group, 0u);

	const u32 again = GraphDocumentOps::AddGroup(document, "Wave", members, Vec2());
	const u32 contents[] = {again, 1, 2};
	EXPECT_EQ(GraphDocumentOps::Delete(document, contents), 3u);
	EXPECT_EQ(document.nodes.size(), 1u);
}

TEST(GraphDocumentTest, GroupLastMemberLeavesKeepsCorner) {
	GraphDocument document = ReadOrFail(Chain);
	const u32 members[] = {2, 3};
	const u32 group = GraphDocumentOps::AddGroup(document, "Tail", members, Vec2(-999.0f, -999.0f));
	const u32 leaving[] = {3};
	EXPECT_TRUE(GraphDocumentOps::SetGroup(document, leaving, 0));
	EXPECT_EQ(document.groups[0].x, -999);
	const u32 last[] = {2};
	EXPECT_TRUE(GraphDocumentOps::SetGroup(document, last, 0));
	EXPECT_EQ(document.groups[0].x, 200);
	EXPECT_EQ(document.groups[0].y, 0);
	EXPECT_FALSE(GraphDocumentOps::SetGroup(document, last, 0));
	EXPECT_FALSE(GraphDocumentOps::SetGroup(document, last, 77));
	const u32 other = GraphDocumentOps::AddGroup(document, "Other", last, Vec2());
	EXPECT_TRUE(GraphDocumentOps::SetGroup(document, last, group));
	EXPECT_EQ(NodeById(document, 2)->group, group);
	(void)other;
}

TEST(GraphDocumentTest, NoteRoundTripsTextLookAndBox) {
	GraphDocument document = ReadOrFail(Chain);
	GraphNote note;
	note.title = "A \"quoted\" title";
	note.body = "first line\nsecond line";
	note.x = 30;
	note.y = -40;
	note.width = 320;
	note.height = 90;
	note.theme = GraphNoteTheme::Dark;
	note.size = GraphNoteSize::Huge;
	const u32 id = GraphDocumentOps::AddNote(document, note);
	ASSERT_NE(id, 0u);
	const str first = GraphDocumentOps::Write(document);
	const GraphDocument read = ReadOrFail(first);
	ASSERT_EQ(read.notes.size(), 1u);
	const GraphNote& back = read.notes[0];
	EXPECT_EQ(back.id, id);
	EXPECT_EQ(back.title, note.title);
	EXPECT_EQ(back.body, note.body);
	EXPECT_EQ(back.x, 30);
	EXPECT_EQ(back.width, 320);
	EXPECT_EQ(back.height, 90);
	EXPECT_EQ(back.theme, GraphNoteTheme::Dark);
	EXPECT_EQ(back.size, GraphNoteSize::Huge);
	EXPECT_EQ(GraphDocumentOps::Write(read), first);

	const u32 holder = GraphDocumentOps::AddGroup(document, "Notes", std::span<const u32>(&id, 1), Vec2());
	GraphNote edited = document.notes[0];
	edited.body = "rewritten";
	edited.group = 0;
	ASSERT_TRUE(GraphDocumentOps::SetNote(document, edited));
	EXPECT_EQ(document.notes[0].body, "rewritten");
	EXPECT_EQ(document.notes[0].group, holder);
}

TEST(GraphDocumentTest, RedirectSplitsAndRejoins) {
	GraphDocument document = ReadOrFail(Chain);
	const u32 ends[] = {2, 3};
	const u32 group = GraphDocumentOps::AddGroup(document, "Wave", ends, Vec2());
	const u32 redirect = GraphDocumentOps::InsertRedirect(document, 3, "A", Vec2(300.0f, 50.0f));
	ASSERT_NE(redirect, 0u);
	const GraphNode* bend = NodeById(document, redirect);
	ASSERT_NE(bend, nullptr);
	EXPECT_EQ(bend->def, GraphDocumentOps::REDIRECT_DEF);
	EXPECT_EQ(bend->group, group);
	const GraphInput* fed = InputOf(*NodeById(document, 3), "A");
	ASSERT_NE(fed, nullptr);
	EXPECT_EQ(fed->fromNode, redirect);
	EXPECT_EQ(fed->fromPort, GraphDocumentOps::REDIRECT_OUT);
	const GraphInput* through = InputOf(*bend, GraphDocumentOps::REDIRECT_IN);
	ASSERT_NE(through, nullptr);
	EXPECT_EQ(through->fromNode, 2u);
	EXPECT_EQ(GraphDocumentOps::InsertRedirect(document, 3, "B", Vec2()), 0u);

	const u32 doomed[] = {redirect};
	EXPECT_EQ(GraphDocumentOps::Delete(document, doomed), 1u);
	fed = InputOf(*NodeById(document, 3), "A");
	ASSERT_NE(fed, nullptr);
	EXPECT_EQ(fed->fromNode, 2u);
	EXPECT_EQ(fed->fromPort, "Out");

	const u32 first = GraphDocumentOps::InsertRedirect(document, 3, "A", Vec2());
	const u32 second = GraphDocumentOps::InsertRedirect(document, 3, "A", Vec2());
	const u32 both[] = {second, first};
	EXPECT_EQ(GraphDocumentOps::Delete(document, both), 2u);
	fed = InputOf(*NodeById(document, 3), "A");
	ASSERT_NE(fed, nullptr);
	EXPECT_EQ(fed->fromNode, 2u);
}

TEST(GraphDocumentTest, GroupCopyRepointsMembership) {
	GraphDocument document = ReadOrFail(Chain);
	const u32 members[] = {1, 2};
	const u32 group = GraphDocumentOps::AddGroup(document, "Wave", members, Vec2());
	GraphNote note;
	note.title = "here";
	const u32 noteId = GraphDocumentOps::AddNote(document, note);
	GraphDocumentOps::SetGroup(document, std::span<const u32>(&noteId, 1), group);

	const str fragment = GraphDocumentOps::Copy(document, std::span<const u32>(&group, 1));
	Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(GraphDocumentOps::Paste(document, fragment, Vec2(0.0f, 300.0f), pasted, error)) << error;
	EXPECT_EQ(pasted.size(), 4u);
	ASSERT_EQ(document.groups.size(), 2u);
	const u32 copy = document.groups[1].id;
	EXPECT_NE(copy, group);
	u32 heldByCopy = 0;
	for (const GraphNode& node : document.nodes) {
		heldByCopy += node.group == copy ? 1u : 0u;
	}
	EXPECT_EQ(heldByCopy, 2u);
	EXPECT_EQ(document.notes.back().group, copy);

	const u32 alone = 1;
	const str single = GraphDocumentOps::Copy(document, std::span<const u32>(&alone, 1));
	ASSERT_TRUE(GraphDocumentOps::Paste(document, single, Vec2(), pasted, error)) << error;
	ASSERT_EQ(pasted.size(), 1u);
	EXPECT_EQ(NodeById(document, pasted[0])->group, 0u);
}

TEST(GraphDocumentTest, OrphanMembershipDropped) {
	const GraphDocument document = ReadOrFail(
		R"({"_type":"NodeShader","version":1,"nodes":[{"id":1,"def":"A","group":40}],"notes":[{"id":2,"group":41}]})");
	ASSERT_EQ(document.nodes.size(), 1u);
	EXPECT_EQ(document.nodes[0].group, 0u);
	ASSERT_EQ(document.notes.size(), 1u);
	EXPECT_EQ(document.notes[0].group, 0u);
}

TEST(GraphDocumentTest, FoldedPreviewRoundTrips) {
	GraphDocument document = ReadOrFail(Canonical);
	ASSERT_FALSE(document.nodes.empty());
	EXPECT_EQ(GraphDocumentOps::Write(document), Canonical);
	EXPECT_EQ(Canonical.find("\"preview\""), std::string_view::npos);

	document.nodes[0].previewCollapsed = true;
	const str folded = GraphDocumentOps::Write(document);
	ASSERT_NE(folded.find("\"preview\":false"), str::npos);
	const GraphDocument reread = ReadOrFail(folded);
	EXPECT_TRUE(reread.nodes[0].previewCollapsed);
	EXPECT_FALSE(reread.nodes[1].previewCollapsed);
	EXPECT_EQ(GraphDocumentOps::Write(reread), folded);
}

namespace {

	constexpr std::string_view Flows = R"({
	"_type": "EffectGraph",
	"version": 2,
	"nextId": 7,
	"settings": {},
	"properties": [],
	"nodes": [],
	"contexts": [
		{"id":1,"def":"Context/Event","x":0,"y":-200,"settings":{"EventName":"Halt"},"blocks":[]},
		{"id":2,"def":"Context/Spawn","x":0,"y":0,"flow":[[1,"Stop"]],"blocks":[]},
		{"id":3,"def":"Context/Spawn","x":200,"y":0,"blocks":[]},
		{"id":4,"def":"Context/Initialize","x":100,"y":200,"flow":[[2,"In"],[3,"In"]],"blocks":[]},
		{"id":5,"def":"Context/Update","x":100,"y":400,"flow":[[4,"In"]],"blocks":[]}
	],
	"groups": [],
	"notes": []
}
)";

	const GraphContext* ContextOf(const GraphDocument& document, u32 id) {
		for (const GraphContext& context : document.contexts) {
			if (context.node.id == id) {
				return &context;
			}
		}
		return nullptr;
	}

} // anonymous namespace

TEST(GraphDocumentTest, FlowLinksNeedVersion2) {
	GraphDocument document = ReadOrFail(Flows);
	const GraphContext* initialize = ContextOf(document, 4);
	ASSERT_NE(initialize, nullptr);
	ASSERT_EQ(initialize->flows.size(), 2u);
	EXPECT_EQ(GraphDocumentOps::Write(document), Flows);

	for (GraphContext& context : document.contexts) {
		context.flows.clear();
	}
	const str unlinked = GraphDocumentOps::Write(document);
	EXPECT_NE(unlinked.find("\"version\": 1,"), str::npos);
	EXPECT_EQ(unlinked.find("\"flow\""), str::npos);
}

TEST(GraphDocumentTest, DanglingFlowLinkDropped) {
	constexpr std::string_view Messy = R"({"_type":"EffectGraph","version":2,
		"nodes":[{"id":9,"def":"Math/Add"}],
		"contexts":[{"id":1,"def":"Context/Spawn"},{"id":2,"def":"Context/Spawn"},
			{"id":3,"def":"Context/Initialize","flow":[[2,"In"],[9,"In"],[44,"In"],[3,"In"],[1,"In"],[2,"In"],["x","In"],[1]]}]})";
	const GraphDocument document = ReadOrFail(Messy);
	const GraphContext* initialize = ContextOf(document, 3);
	ASSERT_NE(initialize, nullptr);
	ASSERT_EQ(initialize->flows.size(), 2u);
	EXPECT_EQ(initialize->flows[0].from, 1u);
	EXPECT_EQ(initialize->flows[1].from, 2u);
	const str first = GraphDocumentOps::Write(document);
	EXPECT_EQ(GraphDocumentOps::Write(ReadOrFail(first)), first);
}

TEST(GraphDocumentTest, DeletingContextCutsFlowLinksItFed) {
	GraphDocument document = ReadOrFail(Flows);
	const u32 doomed[] = {2};
	ASSERT_EQ(GraphDocumentOps::Delete(document, doomed), 1u);
	const GraphContext* initialize = ContextOf(document, 4);
	ASSERT_NE(initialize, nullptr);
	ASSERT_EQ(initialize->flows.size(), 1u);
	EXPECT_EQ(initialize->flows[0].from, 3u);
}

TEST(GraphDocumentTest, CopyKeepsInnerFlowLinks) {
	GraphDocument document = ReadOrFail(Flows);
	const u32 picked[] = {3, 4};
	const str fragment = GraphDocumentOps::Copy(document, picked);
	GraphDocument copied = ReadOrFail(fragment);
	const GraphContext* copiedInitialize = ContextOf(copied, 4);
	ASSERT_NE(copiedInitialize, nullptr);
	ASSERT_EQ(copiedInitialize->flows.size(), 1u);
	EXPECT_EQ(copiedInitialize->flows[0].from, 3u);

	Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
	str error;
	ASSERT_TRUE(GraphDocumentOps::Paste(document, fragment, Vec2(40.0f, 40.0f), pasted, error)) << error;
	ASSERT_EQ(pasted.size(), 2u);
	const GraphContext* pastedInitialize = ContextOf(document, pasted[1]);
	ASSERT_NE(pastedInitialize, nullptr);
	ASSERT_EQ(pastedInitialize->flows.size(), 1u);
	EXPECT_EQ(pastedInitialize->flows[0].from, pasted[0]);
	EXPECT_EQ(pastedInitialize->flows[0].slot, "In");
}

TEST(GraphDocumentTest, LinkEditsTargetContext) {
	GraphDocument document = ReadOrFail(Flows);
	ASSERT_TRUE(GraphDocumentOps::Link(document, 3, 2, "Start"));
	const GraphContext* spawn = ContextOf(document, 2);
	ASSERT_NE(spawn, nullptr);
	ASSERT_EQ(spawn->flows.size(), 2u);
	EXPECT_EQ(spawn->flows[0].slot, "Start");
	EXPECT_FALSE(GraphDocumentOps::Link(document, 3, 2, "Start"));
	EXPECT_FALSE(GraphDocumentOps::Link(document, 2, 2, "Start"));
	EXPECT_FALSE(GraphDocumentOps::Link(document, 99, 2, "Start"));

	ASSERT_TRUE(GraphDocumentOps::Unlink(document, 4, 5, "In"));
	EXPECT_TRUE(ContextOf(document, 5)->flows.empty());
	EXPECT_FALSE(GraphDocumentOps::Unlink(document, 4, 5, "In"));
}

TEST(GraphDocumentTest, BlockMoveCountsOriginalGaps) {
	constexpr std::string_view Stacks = R"({"_type":"EffectGraph","version":2,"nextId":20,
		"contexts":[{"id":1,"def":"Context/Initialize","blocks":[{"id":11,"def":"Block/SetAttribute","in":{"Velocity":{"value":[0,1,0]}}},{"id":12,"def":"Block/SetAttribute"},{"id":13,"def":"Block/SetAttribute"}]},
			{"id":2,"def":"Context/Update","blocks":[{"id":21,"def":"Block/SetAttribute"}]}]})";
	auto order = [](const GraphDocument& document, u32 context) {
		str ids;
		for (const GraphNode& block : ContextOf(document, context)->blocks) {
			ids += std::to_string(block.id) + " ";
		}
		return ids;
	};

	GraphDocument document = ReadOrFail(Stacks);
	const u32 first[] = {11};
	ASSERT_TRUE(GraphDocumentOps::MoveBlocks(document, first, 1, 2));
	EXPECT_EQ(order(document, 1), "12 11 13 ");
	ASSERT_TRUE(GraphDocumentOps::MoveBlocks(document, first, 1, 0));
	EXPECT_EQ(order(document, 1), "11 12 13 ");
	EXPECT_FALSE(GraphDocumentOps::MoveBlocks(document, first, 1, 0));
	EXPECT_FALSE(GraphDocumentOps::MoveBlocks(document, first, 1, 1));

	const u32 pair[] = {11, 13};
	ASSERT_TRUE(GraphDocumentOps::MoveBlocks(document, pair, 2, 1));
	EXPECT_EQ(order(document, 1), "12 ");
	EXPECT_EQ(order(document, 2), "21 11 13 ");
	const GraphNode* moved = GraphDocumentOps::FindNode(document, 11);
	ASSERT_NE(moved, nullptr);
	ASSERT_EQ(moved->inputs.size(), 1u);
	EXPECT_EQ(GraphDocumentOps::ContextOf(document, 11)->node.id, 2u);

	const u32 missing[] = {11, 99};
	EXPECT_FALSE(GraphDocumentOps::MoveBlocks(document, missing, 1, 0));
	EXPECT_EQ(order(document, 2), "21 11 13 ");
}

TEST(GraphDocumentTest, InputSpaceNeedsVersion4) {
	constexpr std::string_view WithBlock = R"({"_type":"EffectGraph","version":3,"nextId":9,"nodes":[{"id":5,"def":"Input/Vector3"}],
		"contexts":[{"id":1,"def":"Context/Initialize","blocks":[
			{"id":8,"def":"Block/SetAttribute","settings":{"Attribute":"position"},"in":{"Position":{"value":[1,2,3]}}},
			{"id":7,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"},"in":{"Velocity":{"from":[5,"Out"]}}}]}]})";
	GraphDocument document = ReadOrFail(WithBlock);
	const str before = GraphDocumentOps::Write(document);
	EXPECT_EQ(before.find("\"space\""), str::npos);
	EXPECT_EQ(before.find("\"version\": 4,"), str::npos);

	ASSERT_TRUE(GraphDocumentOps::SetInputSpace(document, 8, "Position", "World"));
	ASSERT_TRUE(GraphDocumentOps::SetInputSpace(document, 7, "Velocity", "World"));
	EXPECT_FALSE(GraphDocumentOps::SetInputSpace(document, 7, "Velocity", "World"));
	const str world = GraphDocumentOps::Write(document);
	EXPECT_NE(world.find("\"version\": 4,"), str::npos);
	EXPECT_NE(world.find(R"("Position":{"value":[1,2,3],"space":"World"})"), str::npos) << world;
	EXPECT_NE(world.find(R"("Velocity":{"from":[5,"Out"],"space":"World"})"), str::npos) << world;
	GraphDocument reread = ReadOrFail(world);
	EXPECT_EQ(GraphDocumentOps::Write(reread), world);

	ASSERT_TRUE(GraphDocumentOps::SetInputSpace(reread, 8, "Position", ""));
	ASSERT_TRUE(GraphDocumentOps::SetInputSpace(reread, 7, "Velocity", ""));
	EXPECT_EQ(GraphDocumentOps::Write(reread), before);

	ASSERT_TRUE(GraphDocumentOps::SetInputSpace(reread, 8, "Scale", "World"));
	const str spaceOnly = GraphDocumentOps::Write(reread);
	EXPECT_NE(spaceOnly.find(R"("Scale":{"space":"World"})"), str::npos) << spaceOnly;
	EXPECT_EQ(GraphDocumentOps::Write(ReadOrFail(spaceOnly)), spaceOnly);
}

TEST(GraphDocumentTest, DisabledBlockNeedsVersion3) {
	GraphDocument document = ReadOrFail(Flows);
	const str before = GraphDocumentOps::Write(document);
	constexpr std::string_view WithBlock = R"({"_type":"EffectGraph","version":2,"nextId":9,
		"contexts":[{"id":1,"def":"Context/Initialize","blocks":[{"id":8,"def":"Block/SetAttribute","settings":{"Attribute":"velocity"}}]}]})";
	GraphDocument stacked = ReadOrFail(WithBlock);
	EXPECT_FALSE(GraphDocumentOps::SetBlockEnabled(stacked, 8, true));
	EXPECT_FALSE(GraphDocumentOps::SetBlockEnabled(stacked, 1, false));
	ASSERT_TRUE(GraphDocumentOps::SetBlockEnabled(stacked, 8, false));
	const str off = GraphDocumentOps::Write(stacked);
	EXPECT_NE(off.find("\"version\": 3,"), str::npos);
	EXPECT_NE(off.find("\"enabled\":false"), str::npos);
	GraphDocument reread = ReadOrFail(off);
	ASSERT_TRUE(GraphDocumentOps::FindNode(reread, 8)->disabled);
	EXPECT_EQ(GraphDocumentOps::FindNode(reread, 8)->settings.size(), 1u);
	EXPECT_EQ(GraphDocumentOps::Write(reread), off);

	ASSERT_TRUE(GraphDocumentOps::SetBlockEnabled(reread, 8, true));
	const str on = GraphDocumentOps::Write(reread);
	EXPECT_EQ(on.find("\"enabled\""), str::npos);
	EXPECT_EQ(on.find("\"version\": 3,"), str::npos);
	EXPECT_EQ(GraphDocumentOps::Write(ReadOrFail(before)), before);
}
