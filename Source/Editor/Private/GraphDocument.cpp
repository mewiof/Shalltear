#include "Shalltear/Editor/GraphDocument.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Shalltear {

	namespace {

		constexpr f64 MAX_ID = 2147483647.0;

		constexpr f64 MAX_POSITION = 1.0e9;

		str Compact(const JSONValue& value) {
			return JSONSerialize(value, false);
		}

		u32 ReadId(const JSONValue& value) {
			if (!value.IsNumber()) {
				return 0;
			}
			const f64 number = value.AsNumber();
			if (!(number >= 1.0) || number > MAX_ID || std::floor(number) != number) {
				return 0;
			}
			return static_cast<u32>(number);
		}

		i32 ReadPosition(const JSONValue& value) {
			const f64 number = value.AsNumber(0.0);
			if (!std::isfinite(number)) {
				return 0;
			}
			return static_cast<i32>(std::lround(std::clamp(number, -MAX_POSITION, MAX_POSITION)));
		}

		i32 RoundPosition(f32 value) {
			if (!std::isfinite(value)) {
				return 0;
			}
			return static_cast<i32>(std::lround(std::clamp(static_cast<f64>(value), -MAX_POSITION, MAX_POSITION)));
		}

		void ReadSettings(const JSONValue& object, Vector<GraphSetting>& out) {
			for (size_t index = 0; index < object.MemberCount(); ++index) {
				const std::string_view key = object.KeyAt(index);
				str value = Compact(object.ChildAt(index));
				auto existing = std::find_if(out.begin(), out.end(), [&](const GraphSetting& setting) { return setting.key == key; });
				if (existing != out.end()) {
					existing->value = std::move(value);
				} else {
					out.push_back(GraphSetting{str(key), std::move(value)});
				}
			}
			std::sort(out.begin(), out.end(), [](const GraphSetting& a, const GraphSetting& b) { return a.key < b.key; });
		}

		bool Holds(const GraphInput& input) { return input.fromNode != 0 || !input.value.empty() || !input.space.empty(); }

		void ReadInputs(const JSONValue& object, Vector<GraphInput>& out) {
			for (size_t index = 0; index < object.MemberCount(); ++index) {
				const JSONValue& entry = object.ChildAt(index);
				GraphInput input;
				input.port = str(object.KeyAt(index));
				const JSONValue& from = entry["from"];
				if (from.IsArray() && from.Size() == 2 && from[1].IsString()) {
					if (const u32 source = ReadId(from[0])) {
						input.fromNode = source;
						input.fromPort = str(from[1].AsString());
					}
				}
				if (const JSONValue* value = entry.Find("value")) {
					input.value = Compact(*value);
				}
				if (const JSONValue* space = entry.Find("space"); space && space->IsString()) {
					input.space = str(space->AsString());
				}
				if (!Holds(input)) {
					continue;
				}
				auto existing = std::find_if(out.begin(), out.end(), [&](const GraphInput& held) { return held.port == input.port; });
				if (existing != out.end()) {
					*existing = std::move(input);
				} else {
					out.push_back(std::move(input));
				}
			}
			std::sort(out.begin(), out.end(), [](const GraphInput& a, const GraphInput& b) { return a.port < b.port; });
		}

		bool ReadNode(const JSONValue& value, bool positioned, GraphNode& out, str& error) {
			if (!value.IsObject()) {
				error = "an element isn't a JSON object";
				return false;
			}
			out.id = ReadId(value["id"]);
			if (out.id == 0) {
				error = "an element has no valid id";
				return false;
			}
			const JSONValue& def = value["def"];
			if (!def.IsString() || def.AsString().empty()) {
				error = std::format("element {} has no definition", out.id);
				return false;
			}
			out.def = str(def.AsString());
			if (positioned) {
				out.x = ReadPosition(value["x"]);
				out.y = ReadPosition(value["y"]);
				out.group = ReadId(value["group"]);
				out.previewCollapsed = !value["preview"].AsBool(true);
			}
			out.disabled = !value["enabled"].AsBool(true);
			ReadSettings(value["settings"], out.settings);
			ReadInputs(value["in"], out.inputs);
			return true;
		}

		constexpr std::string_view NoteThemes[] = {"light", "dark"};
		constexpr std::string_view NoteSizes[] = {"small", "medium", "large", "huge"};

		template <typename Enum, size_t Count>
		Enum ReadWord(const JSONValue& value, const std::string_view (&words)[Count], Enum fallback) {
			const std::string_view word = value.AsString();
			for (size_t index = 0; index < Count; ++index) {
				if (words[index] == word) {
					return static_cast<Enum>(index);
				}
			}
			return fallback;
		}

		i32 ReadSize(const JSONValue& value, i32 fallback) {
			if (!value.IsNumber()) {
				return fallback;
			}
			return std::max(1, ReadPosition(value));
		}

		void AppendQuoted(str& out, std::string_view text) {
			out += JSONSerialize(JSONValue::MakeString(text), false);
		}

		void AppendSettings(str& out, const Vector<GraphSetting>& settings) {
			out += '{';
			for (size_t index = 0; index < settings.size(); ++index) {
				if (index > 0) {
					out += ',';
				}
				AppendQuoted(out, settings[index].key);
				out += ':';
				out += settings[index].value;
			}
			out += '}';
		}

		void AppendNodeMembers(str& out, const GraphNode& node, bool positioned) {
			out += std::format("{{\"id\":{},\"def\":", node.id);
			AppendQuoted(out, node.def);
			if (positioned) {
				out += std::format(",\"x\":{},\"y\":{}", node.x, node.y);
				if (node.group != 0) {
					out += std::format(",\"group\":{}", node.group);
				}
				if (node.previewCollapsed) {
					out += ",\"preview\":false";
				}
			}
			if (node.disabled) {
				out += ",\"enabled\":false";
			}
			if (!node.settings.empty()) {
				out += ",\"settings\":";
				AppendSettings(out, node.settings);
			}
			bool anyInput = false;
			for (const GraphInput& input : node.inputs) {
				if (!Holds(input)) {
					continue;
				}
				out += anyInput ? "," : ",\"in\":{";
				anyInput = true;
				AppendQuoted(out, input.port);
				out += ":{";
				if (input.fromNode != 0) {
					out += std::format("\"from\":[{},", input.fromNode);
					AppendQuoted(out, input.fromPort);
					out += ']';
				}
				if (!input.value.empty()) {
					out += input.fromNode != 0 ? ",\"value\":" : "\"value\":";
					out += input.value;
				}
				if (!input.space.empty()) {
					out += input.fromNode != 0 || !input.value.empty() ? ",\"space\":" : "\"space\":";
					AppendQuoted(out, input.space);
				}
				out += '}';
			}
			if (anyInput) {
				out += '}';
			}
		}

		template <typename Line>
		void AppendArray(str& out, std::string_view key, size_t count, bool last, Line&& line) {
			out += "\t\"";
			out += key;
			out += "\": [";
			if (count > 0) {
				out += '\n';
				for (size_t index = 0; index < count; ++index) {
					out += "\t\t";
					line(index);
					if (index + 1 < count) {
						out += ',';
					}
					out += '\n';
				}
				out += '\t';
			}
			out += ']';
			out += last ? "\n" : ",\n";
		}

		GraphInput& InputFor(GraphNode& node, std::string_view port) {
			auto at = std::lower_bound(node.inputs.begin(), node.inputs.end(), port,
									   [](const GraphInput& input, std::string_view name) { return input.port < name; });
			if (at != node.inputs.end() && at->port == port) {
				return *at;
			}
			GraphInput input;
			input.port = str(port);
			return *node.inputs.insert(at, std::move(input));
		}

		template <typename Keep>
		void CutEdges(GraphNode& node, Keep&& keep) {
			for (GraphInput& input : node.inputs) {
				if (input.fromNode != 0 && !keep(input.fromNode)) {
					input.fromNode = 0;
					input.fromPort.clear();
				}
			}
			std::erase_if(node.inputs, [](const GraphInput& input) { return !Holds(input); });
		}

		void SortFlows(Vector<GraphFlow>& flows) {
			auto before = [](const GraphFlow& a, const GraphFlow& b) { return a.slot != b.slot ? a.slot < b.slot : a.from < b.from; };
			std::sort(flows.begin(), flows.end(), before);
			flows.erase(std::unique(flows.begin(), flows.end(),
									[](const GraphFlow& a, const GraphFlow& b) { return a.slot == b.slot && a.from == b.from; }),
						flows.end());
		}

		template <typename Keep>
		void CutFlows(GraphContext& context, Keep&& keep) {
			std::erase_if(context.flows, [&](const GraphFlow& flow) { return !keep(flow.from); });
		}

		bool AnyFlow(const GraphDocument& document) {
			return std::any_of(document.contexts.begin(), document.contexts.end(),
							   [](const GraphContext& context) { return !context.flows.empty(); });
		}

		bool AnyDisabled(const GraphDocument& document) {
			const auto disabled = [](const GraphNode& node) { return node.disabled; };
			return std::any_of(document.nodes.begin(), document.nodes.end(), disabled) ||
				   std::any_of(document.contexts.begin(), document.contexts.end(), [&](const GraphContext& context) {
					   return context.node.disabled || std::any_of(context.blocks.begin(), context.blocks.end(), disabled);
				   });
		}

		bool AnySpaced(const GraphDocument& document) {
			const auto spaced = [](const GraphNode& node) {
				return std::any_of(node.inputs.begin(), node.inputs.end(), [](const GraphInput& input) { return !input.space.empty(); });
			};
			return std::any_of(document.nodes.begin(), document.nodes.end(), spaced) ||
				   std::any_of(document.contexts.begin(), document.contexts.end(), [&](const GraphContext& context) {
					   return spaced(context.node) || std::any_of(context.blocks.begin(), context.blocks.end(), spaced);
				   });
		}

		template <typename Visit>
		void ForEachElement(GraphDocument& document, Visit&& visit) {
			for (GraphNode& node : document.nodes) {
				visit(node);
			}
			for (GraphContext& context : document.contexts) {
				visit(context.node);
				for (GraphNode& block : context.blocks) {
					visit(block);
				}
			}
		}

	} // anonymous namespace

	namespace GraphDocumentOps {

		bool Read(std::string_view text, GraphDocument& out, str& error) {
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			const JSONValue root = JSONParse(text, arena);
			if (!root.IsObject()) {
				error = "the text isn't a JSON object";
				return false;
			}
			const JSONValue& type = root["_type"];
			if (!type.IsString() || type.AsString().empty()) {
				error = "the document has no _type";
				return false;
			}
			const i64 version = root["version"].AsInt(0);
			if (version < 1) {
				error = "the document has no format version";
				return false;
			}
			if (version > static_cast<i64>(FORMAT_VERSION)) {
				error = std::format("the document is format version {} and this editor reads up to {}", version, FORMAT_VERSION);
				return false;
			}

			GraphDocument read;
			read.type = str(type.AsString());
			ReadSettings(root["settings"], read.settings);

			const JSONValue& properties = root["properties"];
			for (size_t index = 0; index < properties.Size(); ++index) {
				const JSONValue& row = properties[index];
				GraphProperty property;
				property.id = ReadId(row["id"]);
				if (property.id == 0) {
					error = "a property has no valid id";
					return false;
				}
				property.name = str(row["name"].AsString());
				property.reference = str(row["reference"].AsString());
				property.type = str(row["type"].AsString());
				if (const JSONValue* value = row.Find("default")) {
					property.defaultValue = Compact(*value);
				}
				ReadSettings(row["settings"], property.settings);
				read.properties.push_back(std::move(property));
			}

			const JSONValue& nodes = root["nodes"];
			for (size_t index = 0; index < nodes.Size(); ++index) {
				GraphNode node;
				if (!ReadNode(nodes[index], true, node, error)) {
					return false;
				}
				read.nodes.push_back(std::move(node));
			}

			const JSONValue& contexts = root["contexts"];
			for (size_t index = 0; index < contexts.Size(); ++index) {
				const JSONValue& row = contexts[index];
				GraphContext context;
				if (!ReadNode(row, true, context.node, error)) {
					return false;
				}
				const JSONValue& blocks = row["blocks"];
				for (size_t block = 0; block < blocks.Size(); ++block) {
					GraphNode entry;
					if (!ReadNode(blocks[block], false, entry, error)) {
						return false;
					}
					context.blocks.push_back(std::move(entry));
				}
				const JSONValue& flows = row["flow"];
				for (size_t index = 0; index < flows.Size(); ++index) {
					const JSONValue& link = flows[index];
					GraphFlow flow;
					flow.from = ReadId(link[0]);
					flow.slot = str(link[1].AsString());
					if (flow.from != 0 && !flow.slot.empty()) {
						context.flows.push_back(std::move(flow));
					}
				}
				read.contexts.push_back(std::move(context));
			}

			const JSONValue& groups = root["groups"];
			for (size_t index = 0; index < groups.Size(); ++index) {
				const JSONValue& row = groups[index];
				GraphGroup group;
				group.id = ReadId(row["id"]);
				if (group.id == 0) {
					error = "a group has no valid id";
					return false;
				}
				group.title = str(row["title"].AsString());
				group.x = ReadPosition(row["x"]);
				group.y = ReadPosition(row["y"]);
				read.groups.push_back(std::move(group));
			}
			const JSONValue& notes = root["notes"];
			for (size_t index = 0; index < notes.Size(); ++index) {
				const JSONValue& row = notes[index];
				GraphNote note;
				note.id = ReadId(row["id"]);
				if (note.id == 0) {
					error = "a note has no valid id";
					return false;
				}
				note.title = str(row["title"].AsString());
				note.body = str(row["body"].AsString());
				note.x = ReadPosition(row["x"]);
				note.y = ReadPosition(row["y"]);
				note.width = ReadSize(row["w"], note.width);
				note.height = ReadSize(row["h"], note.height);
				note.theme = ReadWord(row["theme"], NoteThemes, GraphNoteTheme::Light);
				note.size = ReadWord(row["size"], NoteSizes, GraphNoteSize::Medium);
				note.group = ReadId(row["group"]);
				read.notes.push_back(std::move(note));
			}

			HashSet<u32> seen{GetMemoryResource(MemTag::Editor)};
			u32 highest = 0;
			auto claim = [&](u32 id) {
				if (!seen.insert(id).second) {
					error = std::format("id {} is used by 2 elements", id);
					return false;
				}
				highest = std::max(highest, id);
				return true;
			};
			for (const GraphProperty& property : read.properties) {
				if (!claim(property.id)) {
					return false;
				}
			}
			bool unique = true;
			ForEachElement(read, [&](GraphNode& node) { unique = unique && claim(node.id); });
			if (!unique) {
				return false;
			}
			for (const GraphGroup& group : read.groups) {
				if (!claim(group.id)) {
					return false;
				}
			}
			for (const GraphNote& note : read.notes) {
				if (!claim(note.id)) {
					return false;
				}
			}
			HashSet<u32> groupIds{GetMemoryResource(MemTag::Editor)};
			for (const GraphGroup& group : read.groups) {
				groupIds.insert(group.id);
			}
			auto knownGroup = [&](u32& group) {
				if (group != 0 && !groupIds.contains(group)) {
					group = 0;
				}
			};
			for (GraphNode& node : read.nodes) {
				knownGroup(node.group);
			}
			for (GraphContext& context : read.contexts) {
				knownGroup(context.node.group);
			}
			for (GraphNote& note : read.notes) {
				knownGroup(note.group);
			}
			HashSet<u32> contextIds{GetMemoryResource(MemTag::Editor)};
			for (const GraphContext& context : read.contexts) {
				contextIds.insert(context.node.id);
			}
			for (GraphContext& context : read.contexts) {
				const u32 self = context.node.id;
				CutFlows(context, [&](u32 from) { return from != self && contextIds.contains(from); });
				SortFlows(context.flows);
			}

			read.nextId = std::max(ReadId(root["nextId"]), highest + 1);
			out = std::move(read);
			return true;
		}

		str Write(const GraphDocument& document) {
			str out = "{\n\t\"_type\": ";
			AppendQuoted(out, document.type);
			const u32 version = AnySpaced(document) ? 4u : (AnyDisabled(document) ? 3u : (AnyFlow(document) ? 2u : 1u));
			out += std::format(",\n\t\"version\": {},\n\t\"nextId\": {},\n\t\"settings\": ", version, document.nextId);
			AppendSettings(out, document.settings);
			out += ",\n";

			AppendArray(out, "properties", document.properties.size(), false, [&](size_t index) {
				const GraphProperty& property = document.properties[index];
				out += std::format("{{\"id\":{},\"name\":", property.id);
				AppendQuoted(out, property.name);
				out += ",\"reference\":";
				AppendQuoted(out, property.reference);
				out += ",\"type\":";
				AppendQuoted(out, property.type);
				if (!property.defaultValue.empty()) {
					out += ",\"default\":";
					out += property.defaultValue;
				}
				if (!property.settings.empty()) {
					out += ",\"settings\":";
					AppendSettings(out, property.settings);
				}
				out += '}';
			});
			AppendArray(out, "nodes", document.nodes.size(), false, [&](size_t index) {
				AppendNodeMembers(out, document.nodes[index], true);
				out += '}';
			});
			AppendArray(out, "contexts", document.contexts.size(), false, [&](size_t index) {
				const GraphContext& context = document.contexts[index];
				AppendNodeMembers(out, context.node, true);
				if (!context.flows.empty()) {
					out += ",\"flow\":[";
					for (size_t flow = 0; flow < context.flows.size(); ++flow) {
						out += std::format("{}[{},", flow > 0 ? "," : "", context.flows[flow].from);
						AppendQuoted(out, context.flows[flow].slot);
						out += ']';
					}
					out += ']';
				}
				out += ",\"blocks\":[";
				if (!context.blocks.empty()) {
					out += '\n';
					for (size_t block = 0; block < context.blocks.size(); ++block) {
						out += "\t\t\t";
						AppendNodeMembers(out, context.blocks[block], false);
						out += '}';
						if (block + 1 < context.blocks.size()) {
							out += ',';
						}
						out += '\n';
					}
					out += "\t\t";
				}
				out += "]}";
			});
			AppendArray(out, "groups", document.groups.size(), false, [&](size_t index) {
				const GraphGroup& group = document.groups[index];
				out += std::format("{{\"id\":{},\"title\":", group.id);
				AppendQuoted(out, group.title);
				out += std::format(",\"x\":{},\"y\":{}}}", group.x, group.y);
			});
			AppendArray(out, "notes", document.notes.size(), true, [&](size_t index) {
				const GraphNote& note = document.notes[index];
				out += std::format("{{\"id\":{},\"x\":{},\"y\":{},\"w\":{},\"h\":{},\"title\":", note.id, note.x,
								   note.y, note.width, note.height);
				AppendQuoted(out, note.title);
				out += ",\"body\":";
				AppendQuoted(out, note.body);
				out += ",\"theme\":";
				AppendQuoted(out, NoteThemes[static_cast<size_t>(note.theme)]);
				out += ",\"size\":";
				AppendQuoted(out, NoteSizes[static_cast<size_t>(note.size)]);
				if (note.group != 0) {
					out += std::format(",\"group\":{}", note.group);
				}
				out += '}';
			});
			out += "}\n";
			return out;
		}

		GraphNode* FindNode(GraphDocument& document, u32 id) {
			if (id == 0) {
				return nullptr;
			}
			GraphNode* found = nullptr;
			ForEachElement(document, [&](GraphNode& node) {
				if (!found && node.id == id) {
					found = &node;
				}
			});
			return found;
		}

		bool MoveNode(GraphDocument& document, u32 id, Vec2 position) {
			GraphNode* target = nullptr;
			for (GraphNode& node : document.nodes) {
				if (node.id == id) {
					target = &node;
				}
			}
			for (GraphContext& context : document.contexts) {
				if (context.node.id == id) {
					target = &context.node;
				}
			}
			if (target) {
				target->x = RoundPosition(position.x);
				target->y = RoundPosition(position.y);
				return true;
			}
			for (GraphNote& note : document.notes) {
				if (note.id == id) {
					note.x = RoundPosition(position.x);
					note.y = RoundPosition(position.y);
					return true;
				}
			}
			for (GraphGroup& group : document.groups) {
				if (group.id == id) {
					group.x = RoundPosition(position.x);
					group.y = RoundPosition(position.y);
					return true;
				}
			}
			return false;
		}

		bool Connect(GraphDocument& document, u32 fromNode, std::string_view fromPort, u32 toNode, std::string_view toPort) {
			if (fromNode == toNode || fromPort.empty() || toPort.empty() || !FindNode(document, fromNode)) {
				return false;
			}
			GraphNode* target = FindNode(document, toNode);
			if (!target) {
				return false;
			}
			GraphInput& input = InputFor(*target, toPort);
			input.fromNode = fromNode;
			input.fromPort = str(fromPort);
			return true;
		}

		bool Disconnect(GraphDocument& document, u32 toNode, std::string_view toPort) {
			GraphNode* target = FindNode(document, toNode);
			if (!target) {
				return false;
			}
			auto input = std::find_if(target->inputs.begin(), target->inputs.end(),
									  [&](const GraphInput& held) { return held.port == toPort; });
			if (input == target->inputs.end() || input->fromNode == 0) {
				return false;
			}
			input->fromNode = 0;
			input->fromPort.clear();
			if (input->value.empty()) {
				target->inputs.erase(input);
			}
			return true;
		}

		bool Link(GraphDocument& document, u32 from, u32 to, std::string_view slot) {
			if (from == to || slot.empty()) {
				return false;
			}
			GraphContext* source = nullptr;
			GraphContext* target = nullptr;
			for (GraphContext& context : document.contexts) {
				source = context.node.id == from ? &context : source;
				target = context.node.id == to ? &context : target;
			}
			if (!source || !target) {
				return false;
			}
			const bool held = std::any_of(target->flows.begin(), target->flows.end(),
										  [&](const GraphFlow& flow) { return flow.from == from && flow.slot == slot; });
			if (held) {
				return false;
			}
			target->flows.push_back(GraphFlow{from, str(slot)});
			SortFlows(target->flows);
			return true;
		}

		bool Unlink(GraphDocument& document, u32 from, u32 to, std::string_view slot) {
			for (GraphContext& context : document.contexts) {
				if (context.node.id != to) {
					continue;
				}
				return std::erase_if(context.flows, [&](const GraphFlow& flow) { return flow.from == from && flow.slot == slot; }) > 0;
			}
			return false;
		}

		GraphContext* ContextOf(GraphDocument& document, u32 block) {
			for (GraphContext& context : document.contexts) {
				for (const GraphNode& held : context.blocks) {
					if (held.id == block) {
						return &context;
					}
				}
			}
			return nullptr;
		}

		bool MoveBlocks(GraphDocument& document, std::span<const u32> blocks, u32 context, u32 index) {
			GraphContext* target = nullptr;
			for (GraphContext& held : document.contexts) {
				target = held.node.id == context ? &held : target;
			}
			if (!target || blocks.empty()) {
				return false;
			}
			for (u32 id : blocks) {
				if (!ContextOf(document, id)) {
					return false;
				}
			}
			const Vector<GraphNode> before = target->blocks;
			u32 at = std::min<u32>(index, static_cast<u32>(target->blocks.size()));
			for (u32 i = 0; i < at; ++i) {
				if (std::find(blocks.begin(), blocks.end(), target->blocks[i].id) != blocks.end()) {
					--at;
				}
			}
			Vector<GraphNode> moving{GetMemoryResource(MemTag::Editor)};
			for (u32 id : blocks) {
				GraphContext* holder = ContextOf(document, id);
				auto found = std::find_if(holder->blocks.begin(), holder->blocks.end(), [id](const GraphNode& block) { return block.id == id; });
				moving.push_back(std::move(*found));
				holder->blocks.erase(found);
			}
			at = std::min<u32>(at, static_cast<u32>(target->blocks.size()));
			target->blocks.insert(target->blocks.begin() + at, std::make_move_iterator(moving.begin()), std::make_move_iterator(moving.end()));
			if (target->blocks.size() == before.size()) {
				bool same = true;
				for (size_t i = 0; i < before.size() && same; ++i) {
					same = before[i].id == target->blocks[i].id;
				}
				if (same) {
					return false;
				}
			}
			return true;
		}

		bool SetBlockEnabled(GraphDocument& document, u32 block, bool enabled) {
			GraphContext* holder = ContextOf(document, block);
			if (!holder) {
				return false;
			}
			for (GraphNode& held : holder->blocks) {
				if (held.id == block) {
					if (held.disabled == !enabled) {
						return false;
					}
					held.disabled = !enabled;
					return true;
				}
			}
			return false;
		}

		bool SetInputSpace(GraphDocument& document, u32 node, std::string_view port, std::string_view space) {
			GraphNode* target = FindNode(document, node);
			if (!target || port.empty()) {
				return false;
			}
			auto held = std::find_if(target->inputs.begin(), target->inputs.end(), [&](const GraphInput& input) { return input.port == port; });
			if ((held == target->inputs.end() && space.empty()) || (held != target->inputs.end() && held->space == space)) {
				return false;
			}
			InputFor(*target, port).space = str(space);
			std::erase_if(target->inputs, [](const GraphInput& input) { return !Holds(input); });
			return true;
		}

		u32 Delete(GraphDocument& document, std::span<const u32> ids) {
			HashSet<u32> doomed{GetMemoryResource(MemTag::Editor)};
			doomed.insert(ids.begin(), ids.end());
			HashSet<u32> gone{GetMemoryResource(MemTag::Editor)};

			for (const GraphNode& node : document.nodes) {
				if (!doomed.contains(node.id) || node.def != REDIRECT_DEF) {
					continue;
				}
				auto source = std::find_if(node.inputs.begin(), node.inputs.end(),
										   [](const GraphInput& input) { return input.port == REDIRECT_IN; });
				if (source == node.inputs.end() || source->fromNode == 0) {
					continue;
				}
				const u32 fromNode = source->fromNode;
				const str fromPort = source->fromPort;
				const u32 redirect = node.id;
				ForEachElement(document, [&](GraphNode& reader) {
					for (GraphInput& input : reader.inputs) {
						if (input.fromNode == redirect && input.fromPort == REDIRECT_OUT && reader.id != redirect) {
							input.fromNode = fromNode;
							input.fromPort = fromPort;
						}
					}
				});
			}

			std::erase_if(document.groups, [&](const GraphGroup& group) {
				return doomed.contains(group.id) && gone.insert(group.id).second;
			});
			std::erase_if(document.notes, [&](const GraphNote& note) {
				return doomed.contains(note.id) && gone.insert(note.id).second;
			});

			std::erase_if(document.nodes, [&](const GraphNode& node) {
				return doomed.contains(node.id) && gone.insert(node.id).second;
			});
			std::erase_if(document.contexts, [&](const GraphContext& context) {
				if (!doomed.contains(context.node.id)) {
					return false;
				}
				gone.insert(context.node.id);
				for (const GraphNode& block : context.blocks) {
					gone.insert(block.id);
				}
				return true;
			});
			for (GraphContext& context : document.contexts) {
				std::erase_if(context.blocks, [&](const GraphNode& block) {
					return doomed.contains(block.id) && gone.insert(block.id).second;
				});
			}
			if (gone.empty()) {
				return 0;
			}
			ForEachElement(document, [&](GraphNode& node) {
				CutEdges(node, [&](u32 source) { return !gone.contains(source); });
				if (gone.contains(node.group)) {
					node.group = 0;
				}
			});
			for (GraphContext& context : document.contexts) {
				CutFlows(context, [&](u32 source) { return !gone.contains(source); });
			}
			for (GraphNote& note : document.notes) {
				if (gone.contains(note.group)) {
					note.group = 0;
				}
			}
			return static_cast<u32>(gone.size());
		}

		namespace {

			u32* GroupOf(GraphDocument& document, u32 id) {
				for (GraphNode& node : document.nodes) {
					if (node.id == id) {
						return &node.group;
					}
				}
				for (GraphContext& context : document.contexts) {
					if (context.node.id == id) {
						return &context.node.group;
					}
				}
				for (GraphNote& note : document.notes) {
					if (note.id == id) {
						return &note.group;
					}
				}
				return nullptr;
			}

			bool PositionOf(const GraphDocument& document, u32 id, i32& x, i32& y) {
				for (const GraphNode& node : document.nodes) {
					if (node.id == id) {
						x = node.x;
						y = node.y;
						return true;
					}
				}
				for (const GraphContext& context : document.contexts) {
					if (context.node.id == id) {
						x = context.node.x;
						y = context.node.y;
						return true;
					}
				}
				for (const GraphNote& note : document.notes) {
					if (note.id == id) {
						x = note.x;
						y = note.y;
						return true;
					}
				}
				return false;
			}

			GraphGroup* FindGroup(GraphDocument& document, u32 id) {
				for (GraphGroup& group : document.groups) {
					if (group.id == id) {
						return &group;
					}
				}
				return nullptr;
			}

			bool HasMembers(GraphDocument& document, u32 group) {
				bool any = false;
				ForEachElement(document, [&](GraphNode& node) { any = any || node.group == group; });
				return any || std::any_of(document.notes.begin(), document.notes.end(),
										  [group](const GraphNote& note) { return note.group == group; });
			}

		} // anonymous namespace

		u32 AddGroup(GraphDocument& document, std::string_view title, std::span<const u32> members, Vec2 position) {
			GraphGroup group;
			group.id = document.nextId++;
			group.title = str(title);
			group.x = RoundPosition(position.x);
			group.y = RoundPosition(position.y);
			document.groups.push_back(std::move(group));
			SetGroup(document, members, document.groups.back().id);
			return document.groups.back().id;
		}

		bool SetGroup(GraphDocument& document, std::span<const u32> members, u32 group) {
			if (group != 0 && !FindGroup(document, group)) {
				return false;
			}
			HashMap<u32, std::pair<i32, i32>> left{GetMemoryResource(MemTag::Editor)};
			bool changed = false;
			for (const u32 id : members) {
				u32* held = GroupOf(document, id);
				if (!held || *held == group) {
					continue;
				}
				if (*held != 0) {
					i32 x = 0;
					i32 y = 0;
					PositionOf(document, id, x, y);
					auto [at, fresh] = left.try_emplace(*held, x, y);
					if (!fresh) {
						at->second = {std::min(at->second.first, x), std::min(at->second.second, y)};
					}
				}
				*held = group;
				changed = true;
			}
			for (const auto& [id, corner] : left) {
				if (!HasMembers(document, id)) {
					if (GraphGroup* emptied = FindGroup(document, id)) {
						emptied->x = corner.first;
						emptied->y = corner.second;
					}
				}
			}
			return changed;
		}

		bool SetGroupTitle(GraphDocument& document, u32 group, std::string_view title) {
			GraphGroup* found = FindGroup(document, group);
			if (!found || found->title == title) {
				return false;
			}
			found->title = str(title);
			return true;
		}

		u32 AddNote(GraphDocument& document, const GraphNote& note) {
			GraphNote added = note;
			added.id = document.nextId++;
			added.width = std::max(1, added.width);
			added.height = std::max(1, added.height);
			if (added.group != 0 && !FindGroup(document, added.group)) {
				added.group = 0;
			}
			document.notes.push_back(std::move(added));
			return document.notes.back().id;
		}

		bool SetNote(GraphDocument& document, const GraphNote& note) {
			for (GraphNote& held : document.notes) {
				if (held.id != note.id) {
					continue;
				}
				const u32 group = held.group;
				held = note;
				held.group = group;
				held.width = std::max(1, held.width);
				held.height = std::max(1, held.height);
				return true;
			}
			return false;
		}

		u32 InsertRedirect(GraphDocument& document, u32 toNode, std::string_view toPort, Vec2 position) {
			GraphNode* target = FindNode(document, toNode);
			if (!target) {
				return 0;
			}
			auto input = std::find_if(target->inputs.begin(), target->inputs.end(),
									  [&](const GraphInput& held) { return held.port == toPort; });
			if (input == target->inputs.end() || input->fromNode == 0) {
				return 0;
			}
			GraphNode redirect;
			redirect.id = document.nextId++;
			redirect.def = str(REDIRECT_DEF);
			redirect.x = RoundPosition(position.x);
			redirect.y = RoundPosition(position.y);
			GraphInput feed;
			feed.port = str(REDIRECT_IN);
			feed.fromNode = input->fromNode;
			feed.fromPort = input->fromPort;
			redirect.inputs.push_back(std::move(feed));
			const GraphNode* source = FindNode(document, input->fromNode);
			if (source && source->group != 0 && source->group == target->group) {
				redirect.group = source->group;
			}
			input->fromNode = redirect.id;
			input->fromPort = str(REDIRECT_OUT);
			document.nodes.push_back(std::move(redirect));
			return document.nodes.back().id;
		}

		str Copy(const GraphDocument& document, std::span<const u32> ids) {
			HashSet<u32> wanted{GetMemoryResource(MemTag::Editor)};
			wanted.insert(ids.begin(), ids.end());
			HashSet<u32> groups{GetMemoryResource(MemTag::Editor)};
			for (const GraphGroup& group : document.groups) {
				if (wanted.contains(group.id)) {
					groups.insert(group.id);
				}
			}

			GraphDocument fragment;
			fragment.type = document.type;
			for (const GraphNode& node : document.nodes) {
				if (wanted.contains(node.id) || groups.contains(node.group)) {
					fragment.nodes.push_back(node);
				}
			}
			for (const GraphContext& context : document.contexts) {
				if (wanted.contains(context.node.id) || groups.contains(context.node.group)) {
					fragment.contexts.push_back(context);
				}
			}
			for (const GraphNote& note : document.notes) {
				if (wanted.contains(note.id) || groups.contains(note.group)) {
					fragment.notes.push_back(note);
				}
			}
			for (const GraphGroup& group : document.groups) {
				if (groups.contains(group.id)) {
					fragment.groups.push_back(group);
				}
			}
			if (fragment.nodes.empty() && fragment.contexts.empty() && fragment.notes.empty() && fragment.groups.empty()) {
				return {};
			}

			HashSet<u32> inside{GetMemoryResource(MemTag::Editor)};
			u32 highest = 0;
			ForEachElement(fragment, [&](GraphNode& node) {
				inside.insert(node.id);
				highest = std::max(highest, node.id);
			});
			for (const GraphNote& note : fragment.notes) {
				highest = std::max(highest, note.id);
			}
			for (const GraphGroup& group : fragment.groups) {
				highest = std::max(highest, group.id);
			}
			ForEachElement(fragment, [&](GraphNode& node) {
				CutEdges(node, [&](u32 source) { return inside.contains(source); });
				if (!groups.contains(node.group)) {
					node.group = 0;
				}
			});
			for (GraphContext& context : fragment.contexts) {
				CutFlows(context, [&](u32 source) { return inside.contains(source); });
			}
			for (GraphNote& note : fragment.notes) {
				if (!groups.contains(note.group)) {
					note.group = 0;
				}
			}
			fragment.nextId = highest + 1;
			return Write(fragment);
		}

		bool Paste(GraphDocument& document, std::string_view fragment, Vec2 offset, Vector<u32>& pasted, str& error) {
			GraphDocument incoming;
			if (!Read(fragment, incoming, error)) {
				return false;
			}
			if (incoming.type != document.type) {
				error = std::format("a {} fragment can't be pasted into a {} document", incoming.type, document.type);
				return false;
			}
			if (incoming.nodes.empty() && incoming.contexts.empty() && incoming.notes.empty() && incoming.groups.empty()) {
				error = "the fragment holds nothing";
				return false;
			}

			HashMap<u32, u32> remap{GetMemoryResource(MemTag::Editor)};
			u32 next = document.nextId;
			ForEachElement(incoming, [&](GraphNode& node) { remap[node.id] = next++; });
			for (GraphNote& note : incoming.notes) {
				remap[note.id] = next++;
			}
			for (GraphGroup& group : incoming.groups) {
				remap[group.id] = next++;
			}
			auto mapped = [&](u32 id) {
				auto found = remap.find(id);
				return found == remap.end() ? 0u : found->second;
			};
			ForEachElement(incoming, [&](GraphNode& node) {
				node.id = remap[node.id];
				node.group = mapped(node.group);
				for (GraphInput& input : node.inputs) {
					input.fromNode = mapped(input.fromNode);
				}
				CutEdges(node, [](u32) { return true; });
			});
			for (GraphContext& context : incoming.contexts) {
				for (GraphFlow& flow : context.flows) {
					flow.from = mapped(flow.from);
				}
				CutFlows(context, [](u32 source) { return source != 0; });
				SortFlows(context.flows);
			}

			const i32 dx = RoundPosition(offset.x);
			const i32 dy = RoundPosition(offset.y);
			pasted.clear();
			for (GraphNode& node : incoming.nodes) {
				node.x += dx;
				node.y += dy;
				pasted.push_back(node.id);
				document.nodes.push_back(std::move(node));
			}
			for (GraphContext& context : incoming.contexts) {
				context.node.x += dx;
				context.node.y += dy;
				pasted.push_back(context.node.id);
				document.contexts.push_back(std::move(context));
			}
			for (GraphNote& note : incoming.notes) {
				note.id = remap[note.id];
				note.group = mapped(note.group);
				note.x += dx;
				note.y += dy;
				pasted.push_back(note.id);
				document.notes.push_back(std::move(note));
			}
			for (GraphGroup& group : incoming.groups) {
				group.id = remap[group.id];
				group.x += dx;
				group.y += dy;
				pasted.push_back(group.id);
				document.groups.push_back(std::move(group));
			}
			document.nextId = next;
			return true;
		}

		bool Duplicate(GraphDocument& document, std::span<const u32> ids, Vec2 offset, Vector<u32>& pasted) {
			const str fragment = Copy(document, ids);
			if (fragment.empty()) {
				return false;
			}
			str error;
			return Paste(document, fragment, offset, pasted, error);
		}

	} // namespace GraphDocumentOps

} // namespace Shalltear
