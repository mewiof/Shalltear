#include "GraphProperties.h"

#include "GraphHLSL.h"

#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <array>

namespace Shalltear {

	namespace {

		constexpr std::array<std::string_view, 66> RESERVED = {
			"bool", "int", "uint", "dword", "half", "float", "double", "min16float", "min10float", "min16int", "min12int",
			"min16uint", "vector", "matrix", "void", "struct", "typedef", "cbuffer", "tbuffer", "register", "packoffset",
			"Texture2D", "Texture2DArray", "Texture3D", "TextureCube", "TextureCubeArray", "SamplerState", "sampler", "texture", "string", "true", "false",
			"if", "else", "for", "while", "do", "switch", "case", "default", "break", "continue", "return", "discard",
			"in", "out", "inout", "uniform", "static", "const", "extern", "shared", "groupshared", "volatile", "row_major",
			"column_major", "nointerpolation", "linear", "centroid", "noperspective", "sample", "precise", "input",
			"surface", "ShalltearMaterial", "export"};

		str RefuseReference(std::string_view reference) {
			if (reference.empty()) {
				return "has no reference";
			}
			bool identifier = (reference[0] >= 'a' && reference[0] <= 'z') || (reference[0] >= 'A' && reference[0] <= 'Z') || reference[0] == '_';
			for (char c : reference) {
				bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
				identifier = identifier && word;
			}
			if (!identifier) {
				return "has reference '" + str(reference) + "' (not a name a shader can declare: letters/digits/_, not starting with a digit)";
			}
			for (std::string_view reserved : RESERVED) {
				if (reference == reserved) {
					return "has reference '" + str(reference) + "' (the shading language already uses it)";
				}
			}
			bool emitterName = reference.size() > 1 && reference[0] == 'n' && reference[1] >= '0' && reference[1] <= '9';
			if (reference.starts_with("slt") || reference.starts_with("Shalltear") || emitterName) {
				return "has reference '" + str(reference) + "', a prefix the engine reserves";
			}
			return {};
		}

		str RefuseName(std::string_view name) {
			for (char c : name) {
				if (c == '"' || c == '\n' || c == '\r') {
					return "has a name with a quote or a line break (an Inspector label can't show it)";
				}
			}
			return {};
		}

		struct KindName {
			std::string_view name;
			GraphPropertyKind kind;
			u32 components;
		};

		constexpr KindName KINDS[] = {
			{"Float", GraphPropertyKind::Float, 1},
			{"Boolean", GraphPropertyKind::Boolean, 1},
			{"Vector2", GraphPropertyKind::Vector2, 2},
			{"Vector3", GraphPropertyKind::Vector3, 3},
			{"Vector4", GraphPropertyKind::Vector4, 4},
			{"Color", GraphPropertyKind::Color, 4},
			{"Texture2D", GraphPropertyKind::Texture2D, 0},
			{"BooleanKeyword", GraphPropertyKind::BooleanKeyword, 1},
			{"Texture2DArray", GraphPropertyKind::Texture2DArray, 0},
			{"Texture3D", GraphPropertyKind::Texture3D, 0},
			{"Cubemap", GraphPropertyKind::Cubemap, 0},
			{"EnumKeyword", GraphPropertyKind::EnumKeyword, 1},
		};

		str RefuseEntries(const GraphPropertyInfo& info) {
			const size_t count = info.entries.size();
			if (count < GRAPH_KEYWORD_MIN_ENTRIES || count > GRAPH_KEYWORD_MAX_ENTRIES) {
				return "has " + std::to_string(count) + (count == 1 ? " entry" : " entries") + " (an enum keyword has " +
					   std::to_string(GRAPH_KEYWORD_MIN_ENTRIES) + "-" + std::to_string(GRAPH_KEYWORD_MAX_ENTRIES) + ")";
			}
			for (size_t i = 0; i < count; ++i) {
				const GraphKeywordEntry& entry = info.entries[i];
				if (entry.name.empty() || !RefuseName(entry.name).empty()) {
					return "has an entry with an empty name, or a quote/line break in it (an Inspector label can't show it)";
				}
				bool word = !entry.reference.empty();
				for (char c : entry.reference) {
					word = word && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
				}
				const str keyword = GraphProperties::EntryKeyword(info, i);
				if (!word || !RefuseReference(keyword).empty()) {
					return "has entry '" + entry.name + "' (its keyword '" + keyword + "' isn't a name a shader can define)";
				}
				for (size_t j = 0; j < i; ++j) {
					if (info.entries[j].id == entry.id) {
						return "has 2 entries with the id " + std::to_string(entry.id);
					}
					if (info.entries[j].reference == entry.reference) {
						return "has entries '" + info.entries[j].name + "' and '" + entry.name + "', which both enable '" + keyword + "'";
					}
				}
			}
			const f32 index = info.defaultValue[0];
			if (index != static_cast<f32>(static_cast<i32>(index)) || index < 0.0f || index >= static_cast<f32>(count)) {
				return "has a default that is not one of its entries";
			}
			return {};
		}

		void KeywordsOf(const GraphPropertyInfo& info, Vector<str>& out) {
			out.clear();
			if (info.kind == GraphPropertyKind::BooleanKeyword) {
				out.push_back(info.reference);
			}
			for (size_t i = 0; info.kind == GraphPropertyKind::EnumKeyword && i < info.entries.size(); ++i) {
				out.push_back(GraphProperties::EntryKeyword(info, i));
			}
		}

		str DeclaredName(const GraphPropertyInfo& info) {
			return GraphProperties::IsTexture(info.kind) ? info.reference : GraphProperties::MemberName(info);
		}

		std::string_view LaterType(std::string_view type) {
			if (type == "Matrix2" || type == "Matrix3" || type == "Matrix4" || type == "Gradient") {
				return "is a graph-local constant (arrives with the matrix and gradient inputs)";
			}
			if (type == "SamplerState") {
				return "isn't supported (filtering comes with the texture)";
			}
			return {};
		}

		const JSONValue* FindSetting(const GraphProperty& property, std::string_view key, LinearAllocator& arena, JSONValue& storage) {
			for (const GraphSetting& setting : property.settings) {
				if (setting.key == key) {
					storage = JSONParse(setting.value, arena);
					return &storage;
				}
			}
			return nullptr;
		}

	} // anonymous namespace

	namespace GraphProperties {

		bool Parse(const GraphProperty& property, GraphPropertyInfo& out, str& error) {
			out = GraphPropertyInfo{};
			out.id = property.id;
			out.name = property.name;
			out.reference = property.reference;

			const KindName* kind = nullptr;
			for (const KindName& candidate : KINDS) {
				if (candidate.name == property.type) {
					kind = &candidate;
				}
			}
			if (kind == nullptr) {
				std::string_view later = LaterType(property.type);
				error = later.empty() ? "is a '" + property.type + "' (not a property type)" : "is a " + property.type + ", which " + str(later);
				return false;
			}
			out.kind = kind->kind;
			if (str refused = RefuseReference(property.reference); !refused.empty()) {
				error = std::move(refused);
				return false;
			}
			if (str refused = RefuseName(property.name); !refused.empty()) {
				error = std::move(refused);
				return false;
			}

			if (out.kind == GraphPropertyKind::Color) {
				out.defaultValue[3] = 1.0f;
			}
			LinearAllocator arena(property.defaultValue.size() + 256, MemTag::Editor);
			if (!property.defaultValue.empty()) {
				JSONValue value = JSONParse(property.defaultValue, arena);
				if (value.IsBool()) {
					out.defaultValue[0] = value.AsBool() ? 1.0f : 0.0f;
				} else if (value.IsNumber()) {
					out.defaultValue[0] = static_cast<f32>(value.AsNumber());
				} else if (value.IsArray() && value.Size() <= 4) {
					for (size_t i = 0; i < value.Size(); ++i) {
						if (!value[i].IsNumber()) {
							error = "has a default that isn't a list of numbers";
							return false;
						}
						out.defaultValue[i] = static_cast<f32>(value[i].AsNumber());
					}
				} else if (!IsTexture(out.kind)) {
					error = "has a default that isn't a number, a list of numbers or true or false";
					return false;
				}
			}

			size_t settingsBytes = 256;
			for (const GraphSetting& setting : property.settings) {
				settingsBytes += setting.value.size() * 2 + 64;
			}
			LinearAllocator settingsArena(settingsBytes, MemTag::Editor);
			JSONValue storage;
			if (const JSONValue* hidden = FindSetting(property, "hidden", settingsArena, storage)) {
				out.hidden = hidden->IsBool() && hidden->AsBool();
			}
			if (const JSONValue* group = FindSetting(property, "group", settingsArena, storage); group && group->IsString()) {
				out.group = str(group->AsString());
				if (str refused = RefuseName(out.group); !refused.empty()) {
					error = "is in a group with a quote or a line break in its name (an Inspector foldout can't show it)";
					return false;
				}
			}
			if (const JSONValue* binding = FindSetting(property, "customBinding", settingsArena, storage)) {
				out.customBinding = binding->IsBool() && binding->AsBool();
			}
			if (const JSONValue* label = FindSetting(property, "bindingLabel", settingsArena, storage); label && label->IsString()) {
				out.bindingLabel = str(label->AsString());
			}
			str mode;
			if (const JSONValue* stored = FindSetting(property, "mode", settingsArena, storage); stored && stored->IsString()) {
				mode = str(stored->AsString());
			}
			if (!mode.empty() && mode != "default") {
				bool known = false;
				switch (out.kind) {
				case GraphPropertyKind::Float:
					if (mode == "slider") {
						out.slider = known = true;
					} else if (mode == "integer") {
						out.kind = GraphPropertyKind::Integer;
						known = true;
					}
					break;
				case GraphPropertyKind::Color:
					out.hdr = known = mode == "hdr";
					break;
				case GraphPropertyKind::Texture2D:
					known = mode == "white" || mode == "black" || mode == "normal" || mode == "transparent" || mode == "gray";
					if (known) {
						out.fallback = mode;
					}
					break;
				default:
					break;
				}
				if (!known) {
					error = "has mode '" + mode + "', which a " + property.type + " doesn't have";
					return false;
				}
			}
			if (out.slider) {
				const JSONValue* low = FindSetting(property, "min", settingsArena, storage);
				out.minValue = low && low->IsNumber() ? static_cast<f32>(low->AsNumber()) : 0.0f;
				const JSONValue* high = FindSetting(property, "max", settingsArena, storage);
				out.maxValue = high && high->IsNumber() ? static_cast<f32>(high->AsNumber()) : 1.0f;
				if (!(out.minValue < out.maxValue)) {
					error = "is a slider with a minimum that isn't below its maximum";
					return false;
				}
			}
			if (out.kind == GraphPropertyKind::Integer) {
				out.defaultValue[0] = static_cast<f32>(static_cast<i32>(out.defaultValue[0]));
			}
			if (out.kind == GraphPropertyKind::EnumKeyword) {
				if (!ReadEntries(property, out.entries, error)) {
					return false;
				}
				if (str refused = RefuseEntries(out); !refused.empty()) {
					error = std::move(refused);
					return false;
				}
			}
			return true;
		}

		GraphValueType ValueType(GraphPropertyKind kind) {
			switch (kind) {
			case GraphPropertyKind::Float:
			case GraphPropertyKind::Integer:
				return GraphValueType::Float;
			case GraphPropertyKind::Boolean:
				return GraphValueType::Bool;
			case GraphPropertyKind::Vector2:
				return GraphValueType::Float2;
			case GraphPropertyKind::Vector3:
				return GraphValueType::Float3;
			case GraphPropertyKind::Vector4:
			case GraphPropertyKind::Color:
				return GraphValueType::Float4;
			case GraphPropertyKind::Texture2D:
				return GraphValueType::Texture2D;
			case GraphPropertyKind::Texture2DArray:
				return GraphValueType::Texture2DArray;
			case GraphPropertyKind::Texture3D:
				return GraphValueType::Texture3D;
			case GraphPropertyKind::Cubemap:
				return GraphValueType::TextureCube;
			case GraphPropertyKind::BooleanKeyword:
			case GraphPropertyKind::EnumKeyword:
				break;
			}
			return GraphValueType::None;
		}

		bool IsTexture(GraphPropertyKind kind) { return GraphTypes::IsTexture(ValueType(kind)); }

		bool IsKeyword(GraphPropertyKind kind) { return kind == GraphPropertyKind::BooleanKeyword || kind == GraphPropertyKind::EnumKeyword; }

		str MemberName(const GraphPropertyInfo& info) {
			if (IsTexture(info.kind)) {
				return {};
			}
			switch (info.kind) {
			case GraphPropertyKind::BooleanKeyword:
				return info.reference + "_Toggle";
			default:
				return info.reference;
			}
		}

		std::string_view MemberType(GraphPropertyKind kind) {
			switch (kind) {
			case GraphPropertyKind::Float:
				return "float";
			case GraphPropertyKind::Integer:
			case GraphPropertyKind::Boolean:
			case GraphPropertyKind::BooleanKeyword:
			case GraphPropertyKind::EnumKeyword:
				return "int";
			case GraphPropertyKind::Vector2:
				return "float2";
			case GraphPropertyKind::Vector3:
				return "float3";
			case GraphPropertyKind::Vector4:
			case GraphPropertyKind::Color:
				return "float4";
			case GraphPropertyKind::Texture2D:
			case GraphPropertyKind::Texture2DArray:
			case GraphPropertyKind::Texture3D:
			case GraphPropertyKind::Cubemap:
				break;
			}
			return {};
		}

		str ReadExpression(const GraphPropertyInfo& info) {
			switch (info.kind) {
			case GraphPropertyKind::Integer:
				return "(float)" + info.reference;
			case GraphPropertyKind::Boolean:
				return info.reference + " != 0";
			default:
				return info.reference;
			}
		}

		str SamplerName(const GraphPropertyInfo& info) { return "sltSampler_" + info.reference; }

		str EntryKeyword(const GraphPropertyInfo& info, size_t entry) { return info.reference + "_" + info.entries[entry].reference; }

		str DefaultKeyword(const GraphPropertyInfo& info) {
			const size_t index = static_cast<size_t>(info.defaultValue[0]);
			if (info.kind == GraphPropertyKind::EnumKeyword && index < info.entries.size()) {
				return EntryKeyword(info, index);
			}
			return info.kind == GraphPropertyKind::BooleanKeyword && info.defaultValue[0] != 0.0f ? info.reference : str();
		}

		bool ReadEntries(const GraphProperty& property, Vector<GraphKeywordEntry>& out, str& error) {
			out.clear();
			const GraphSetting* setting = nullptr;
			for (const GraphSetting& held : property.settings) {
				setting = held.key == "entries" ? &held : setting;
			}
			if (!setting) {
				error = "has no entries";
				return false;
			}
			LinearAllocator arena(setting->value.size() * 2 + 256, MemTag::Editor);
			const JSONValue list = JSONParse(setting->value, arena);
			if (!list.IsArray()) {
				error = "has entries that aren't a list";
				return false;
			}
			for (size_t i = 0; i < list.Size(); ++i) {
				const JSONValue& entry = list[i];
				const JSONValue* id = entry.IsObject() ? entry.Find("id") : nullptr;
				const JSONValue* name = entry.IsObject() ? entry.Find("name") : nullptr;
				const JSONValue* reference = entry.IsObject() ? entry.Find("reference") : nullptr;
				if (!id || !id->IsNumber() || id->AsNumber() < 1.0 || !name || !name->IsString() || !reference || !reference->IsString()) {
					error = "has an entry " + std::to_string(i) + " that isn't an id, a name and a reference";
					return false;
				}
				out.push_back({static_cast<u32>(id->AsNumber()), str(name->AsString()), str(reference->AsString())});
			}
			return true;
		}

		str WriteEntries(std::span<const GraphKeywordEntry> entries) {
			JSONValue list = JSONValue::MakeArray();
			for (const GraphKeywordEntry& held : entries) {
				JSONValue entry = JSONValue::MakeObject();
				entry.AddMember("id", JSONValue::MakeNumber(held.id));
				entry.AddMember("name", JSONValue::MakeString(held.name));
				entry.AddMember("reference", JSONValue::MakeString(held.reference));
				list.Push(std::move(entry));
			}
			return JSONSerialize(list, false);
		}

		str Clash(const GraphPropertyInfo& a, const GraphPropertyInfo& b) {
			if (a.reference == b.reference) {
				return "are 2 properties with the reference '" + a.reference + "' (a material stores 1 value per name)";
			}
			const str member = MemberName(a);
			if (!member.empty() && member == MemberName(b)) {
				return "both declare a member '" + member + "'";
			}
			Vector<str> keywords{GetMemoryResource(MemTag::Editor)};
			Vector<str> others{GetMemoryResource(MemTag::Editor)};
			KeywordsOf(a, keywords);
			KeywordsOf(b, others);
			for (const str& keyword : keywords) {
				if (std::find(others.begin(), others.end(), keyword) != others.end()) {
					return "both enable a keyword '" + keyword + "'";
				}
				if (keyword == DeclaredName(b)) {
					return "meet at '" + keyword + "' (a keyword of one and a name the other declares, the keyword would rewrite it)";
				}
			}
			for (const str& keyword : others) {
				if (keyword == DeclaredName(a)) {
					return "meet at '" + keyword + "' (a keyword of one and a name the other declares, the keyword would rewrite it)";
				}
			}
			return {};
		}

		bool ParseBlackboard(const GraphDocument& document, Vector<GraphPropertyInfo>& out, Vector<GraphDiagnostic>& diagnostics) {
			const size_t firstDiagnostic = diagnostics.size();
			u32 textures = 0;
			for (const GraphProperty& property : document.properties) {
				GraphPropertyInfo info;
				str error;
				if (!Parse(property, info, error)) {
					diagnostics.push_back({0, "the blackboard's '" + property.name + "' " + error});
					continue;
				}
				for (const GraphPropertyInfo& earlier : out) {
					if (str clash = Clash(earlier, info); !clash.empty()) {
						diagnostics.push_back({0, "the blackboard's '" + info.name + "' and '" + earlier.name + "' " + clash});
					}
				}
				textures += IsTexture(info.kind) ? 1 : 0;
				out.push_back(std::move(info));
			}
			constexpr u32 MAX_TEXTURES = (MAX_MATERIAL_SET_RESOURCES - 1) / 2;
			if (textures > MAX_TEXTURES) {
				diagnostics.push_back({0, "the blackboard declares " + std::to_string(textures) + " textures (a material binds at most " +
											  std::to_string(MAX_TEXTURES) + ")"});
			}
			return diagnostics.size() == firstDiagnostic;
		}

		str Declarations(std::span<const GraphPropertyInfo> properties) {
			str text;
			bool anyMember = false;
			for (const GraphPropertyInfo& property : properties) {
				anyMember = anyMember || !IsTexture(property.kind);
			}
			if (anyMember) {
				text += "\n[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {\n";
				for (const GraphPropertyInfo& property : properties) {
					if (!IsTexture(property.kind)) {
						text += "\t" + str(MemberType(property.kind)) + " " + MemberName(property) + ";\n";
					}
				}
				text += "};\n";
			}
			u32 binding = 1;
			for (const GraphPropertyInfo& property : properties) {
				if (!IsTexture(property.kind)) {
					continue;
				}
				text += "[[vk::binding(" + std::to_string(binding) + ", 1)]] " + str(GraphTypes::HLSLName(ValueType(property.kind))) + " " +
						property.reference + ";\n";
				text += "[[vk::binding(" + std::to_string(binding + 1) + ", 1)]] SamplerState " + SamplerName(property) + ";\n";
				binding += 2;
			}
			text += "\n";
			return text;
		}

		str HostDeclarations(std::span<const GraphPropertyInfo> properties, u32 firstTexture) {
			str text;
			for (const GraphPropertyInfo& property : properties) {
				if (IsTexture(property.kind)) {
					continue;
				}
				const std::string_view type = MemberType(property.kind);
				str value;
				if (type == "int") {
					value = std::to_string(static_cast<i32>(property.defaultValue[0]));
				} else {
					const u32 count = type == "float" ? 1u : static_cast<u32>(type.back() - '0');
					value = count == 1 ? GraphHLSL::FloatLiteral(property.defaultValue[0]) : str(type) + "(";
					for (u32 i = 0; count > 1 && i < count; ++i) {
						value += (i == 0 ? "" : ", ") + GraphHLSL::FloatLiteral(property.defaultValue[i]);
					}
					value += count > 1 ? ")" : "";
				}
				text += "static " + str(type) + " " + MemberName(property) + " = " + value + ";\n";
			}
			u32 binding = firstTexture;
			for (const GraphPropertyInfo& property : properties) {
				if (!IsTexture(property.kind)) {
					continue;
				}
				text += "[[vk::binding(" + std::to_string(binding) + ", 1)]] " + str(GraphTypes::HLSLName(ValueType(property.kind))) + " " +
						property.reference + ";\n";
				text += "[[vk::binding(" + std::to_string(binding + 1) + ", 1)]] SamplerState " + SamplerName(property) + ";\n";
				binding += 2;
			}
			text += "\n";
			return text;
		}

	} // namespace GraphProperties

} // namespace Shalltear
