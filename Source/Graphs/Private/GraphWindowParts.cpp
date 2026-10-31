#include "GraphWindowParts.h"

#include "GraphCurves.h"
#include "GraphDefinitions.h"
#include "GraphProperties.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringUtil.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Editor/CurveEditor.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/ObjectField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <memory>

namespace Shalltear::GraphWindowParts {

	using EditorTheme::ColorToken;
	using EditorTheme::MetricToken;
	namespace Ops = NodeShaderEditOps;

	str Numbers(const f32* values, u32 count) {
		if (count == 1) {
			return std::format("{}", values[0]);
		}
		str out = "[";
		for (u32 i = 0; i < count; ++i) {
			out += (i == 0 ? "" : ",") + std::format("{}", values[i]);
		}
		return out + "]";
	}

	u32 ReadNumbers(std::string_view json, f32 out[4]) {
		if (json.empty()) {
			return 0;
		}
		LinearAllocator arena(json.size() + 64, MemTag::Editor);
		JSONValue value = JSONParse(json, arena);
		if (value.IsNumber()) {
			out[0] = static_cast<f32>(value.AsNumber());
			return 1;
		}
		if (value.IsBool()) {
			out[0] = value.AsBool() ? 1.0f : 0.0f;
			return 1;
		}
		u32 count = 0;
		if (value.IsArray()) {
			for (size_t i = 0; i < value.Size() && i < 4; ++i) {
				out[i] = value[i].IsNumber() ? static_cast<f32>(value[i].AsNumber()) : 0.0f;
				++count;
			}
		}
		return count;
	}

	str ReadString(std::string_view json) {
		if (json.empty()) {
			return {};
		}
		LinearAllocator arena(json.size() + 64, MemTag::Editor);
		JSONValue value = JSONParse(json, arena);
		return value.IsString() ? str(value.AsString()) : str();
	}

	const GraphInput* FindInput(const GraphNode& node, std::string_view port) {
		for (const GraphInput& input : node.inputs) {
			if (input.port == port) {
				return &input;
			}
		}
		return nullptr;
	}

	const GraphSetting* FindSetting(const Vector<GraphSetting>& settings, std::string_view key) {
		for (const GraphSetting& setting : settings) {
			if (setting.key == key) {
				return &setting;
			}
		}
		return nullptr;
	}

	const GraphProperty* FindProperty(const GraphDocument& document, u32 id) {
		for (const GraphProperty& property : document.properties) {
			if (property.id == id) {
				return &property;
			}
		}
		return nullptr;
	}

	void ActiveInputs(const GraphNodeDefinition& definition, const GraphNode& node, Vector<u32>& out) {
		out.clear();
		for (u32 i = 0; i < definition.inputs.size(); ++i) {
			if (GraphNodeLibrary::InputActive(definition, i, node)) {
				out.push_back(i);
			}
		}
	}

	GraphInlineValue InlineFor(const GraphNode& node, const GraphPortDefinition& port, u32 dynamicWidth, bool color) {
		GraphInlineValue value;
		if (!port.binding.empty() || !port.customBinding.empty()) {
			value.kind = GraphInlineKind::Label;
			value.text = str(port.customBinding.empty() ? port.binding : port.customBinding);
			return value;
		}
		u32 width = 0;
		switch (port.type) {
		case GraphPortType::Float:
			width = 1;
			break;
		case GraphPortType::Float2:
			width = 2;
			break;
		case GraphPortType::Float3:
			width = 3;
			break;
		case GraphPortType::Float4:
			width = 4;
			break;
		case GraphPortType::DynamicVector:
		case GraphPortType::Dynamic:
			width = dynamicWidth;
			break;
		case GraphPortType::Boolean:
			value.kind = GraphInlineKind::Toggle;
			value.count = 1;
			break;
		case GraphPortType::Curve: {
			value.kind = GraphInlineKind::Curve;
			const GraphInput* stored = FindInput(node, port.name);
			std::string_view json = stored && !stored->value.empty() ? std::string_view(stored->value) : port.defaultJson;
			str error;
			GraphCurves::Parse(json.empty() ? GraphCurves::DEFAULT_JSON : json, value.curve, error);
			return value;
		}
		case GraphPortType::Gradient: {
			value.kind = GraphInlineKind::Gradient;
			const GraphInput* stored = FindInput(node, port.name);
			const std::string_view json = stored && !stored->value.empty() ? std::string_view(stored->value) : port.defaultJson;
			std::array<f32, GRAPH_GRADIENT_COMPONENTS> gradient{};
			GraphGradients::Default(gradient);
			str error;
			if (!json.empty() && !GraphGradients::Read(json, gradient, error)) {
				GraphGradients::Default(gradient);
			}
			constexpr u32 STEPS = 32;
			for (u32 step = 0; step < STEPS; ++step) {
				f32 rgba[4];
				GraphGradients::Evaluate(gradient, (static_cast<f32>(step) + 0.5f) / static_cast<f32>(STEPS), rgba);
				value.ramp.push_back(Color(rgba[0], rgba[1], rgba[2], rgba[3]));
			}
			return value;
		}
		default:
			return value;
		}
		for (u32 i = 0; i < 4; ++i) {
			value.values[i] = port.defaultValue[i];
		}
		if (const GraphInput* stored = FindInput(node, port.name); stored && !stored->value.empty()) {
			f32 read[4] = {value.values[0], value.values[1], value.values[2], value.values[3]};
			const u32 count = ReadNumbers(stored->value, read);
			for (u32 i = 0; i < count; ++i) {
				value.values[i] = read[i];
			}
		}
		if (value.kind == GraphInlineKind::Toggle) {
			return value;
		}
		if (color) {
			value.kind = GraphInlineKind::Color;
			value.count = 4;
			if (width < 4) {
				value.values[3] = 1.0f;
			}
			return value;
		}
		value.kind = GraphInlineKind::Numbers;
		value.count = static_cast<u8>(width);
		return value;
	}

	str SettingText(const GraphNode& node, const GraphSettingDefinition& setting) {
		const GraphSetting* stored = FindSetting(node.settings, setting.key);
		const str json = stored ? stored->value : str();
		switch (setting.kind) {
		case GraphSettingKind::Choice: {
			const str chosen = ReadString(json);
			if (!chosen.empty()) {
				return chosen;
			}
			return setting.defaultIndex < setting.options.size() ? str(setting.options[setting.defaultIndex]) : str();
		}
		case GraphSettingKind::Toggle: {
			f32 value[4] = {static_cast<f32>(setting.defaultIndex), 0.0f, 0.0f, 0.0f};
			ReadNumbers(json, value);
			return value[0] != 0.0f ? "On" : "Off";
		}
		case GraphSettingKind::Text:
			return json.empty() ? str(setting.defaultText) : ReadString(json);
		case GraphSettingKind::Number: {
			f32 value[4] = {setting.defaultValue[0], 0.0f, 0.0f, 0.0f};
			ReadNumbers(json, value);
			return std::format("{}", value[0]);
		}
		case GraphSettingKind::Asset:
			return AssetCaption(setting, json);
		default:
			return {};
		}
	}

	Guid HeldAsset(const GraphNode& node, const GraphSettingDefinition& setting) {
		const GraphSetting* stored = FindSetting(node.settings, setting.key);
		const str text = stored ? ReadString(stored->value) : str();
		return text.empty() ? Guid{} : Guid::FromString(text);
	}

	str AssetCaption(const GraphSettingDefinition& setting, std::string_view json) {
		if (json.empty()) {
			return str(setting.defaultText);
		}
		const str text = ReadString(json);
		if (text.empty()) {
			return "None";
		}
		const Path path = AssetDatabase::LookupPath(Guid::FromString(text));
		return path.ToString().empty() ? text + " (missing)" : str(std::filesystem::path(path.ToString()).stem().generic_string());
	}

	bool TakesAsset(const GraphSettingDefinition& setting, const UI::DragPayload& payload) {
		if (payload.type != "Asset"_hash || !payload.data.Is<AssetDragData>()) {
			return false;
		}
		const Path path = AssetDatabase::LookupPath(payload.data.As<AssetDragData>().guid);
		const str extension = StringUtil::ToLower(std::filesystem::path(path.ToString()).extension().generic_string());
		return std::find(setting.options.begin(), setting.options.end(), std::string_view(extension)) != setting.options.end();
	}

	UI::ElementHandle AddText(UI::Context& context, UI::ElementHandle parent, ColorToken color, MetricToken size, std::string_view text) {
		UI::ElementHandle line = context.CreateElement();
		context.AppendChild(parent, line);
		context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(size), EditorTheme::GetColor(color), text);
		return line;
	}

	UI::ElementHandle AddRow(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle row = context.CreateElement();
		context.AppendChild(parent, row);
		UI::Style style;
		style.flexDirection = UI::FlexDirection::Row;
		style.alignItems = UI::Align::Center;
		style.width = UI::Length::Percent(1.0f);
		style.margin = {0.0f, 0.0f, 0.0f, 4.0f};
		context.SetStyle(row, style);
		return row;
	}

	UI::ElementHandle AddField(UI::Context& context, UI::ElementHandle parent, std::string_view label) {
		UI::ElementHandle row = AddRow(context, parent);
		UI::ElementHandle caption = AddText(context, row, ColorToken::Text, MetricToken::FontRow, label);
		UI::Style style;
		style.width = UI::Length::Pixel(96.0f);
		style.flexShrink = 0.0f;
		context.SetStyle(caption, style);
		return row;
	}

	f32 FontRow() { return EditorTheme::GetMetric(MetricToken::FontRow); }

	void BuildSettings(UI::Context& context, UI::ElementHandle parent, u32 id, std::span<const GraphSettingDefinition> settings,
					   const Editor& editor) {
		GraphDocument* document = editor.document ? editor.document() : nullptr;
		GraphNode* node = document ? GraphDocumentOps::FindNode(*document, id) : nullptr;
		if (!node) {
			return;
		}
		for (const GraphSettingDefinition& setting : settings) {
			if (!GraphNodeLibrary::SettingActive(settings, static_cast<u32>(&setting - settings.data()), *node)) {
				continue;
			}
			const GraphSetting* stored = FindSetting(node->settings, setting.key);
			const str json = stored ? stored->value : str();
			const str key(setting.key);
			UI::ElementHandle row = AddField(context, parent, setting.key);
			switch (setting.kind) {
			case GraphSettingKind::Choice: {
				const str chosen = ReadString(json);
				u32 selected = setting.defaultIndex;
				for (u32 i = 0; i < setting.options.size(); ++i) {
					selected = setting.options[i] == chosen ? i : selected;
				}
				UI::Widgets::DropdownDesc desc;
				desc.items = setting.options;
				desc.selected = selected;
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = FontRow();
				desc.onSelect = [editor, id, key, options = setting.options](u32 index) {
					if (index < options.size()) {
						editor.edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, Ops::Quote(options[index])); });
					}
				};
				UI::Widgets::Dropdown(context, row, desc);
				break;
			}
			case GraphSettingKind::Toggle: {
				f32 value[4] = {static_cast<f32>(setting.defaultIndex), 0.0f, 0.0f, 0.0f};
				ReadNumbers(json, value);
				UI::Widgets::Checkbox(context, row, value[0] != 0.0f, [editor, id, key](bool on) {
					editor.edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, on ? "true" : "false"); });
				});
				break;
			}
			case GraphSettingKind::Text: {
				const str current = json.empty() ? str(setting.defaultText) : ReadString(json);
				UI::Widgets::TextFieldDesc desc;
				desc.text = current;
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = FontRow();
				desc.multiline = setting.multiline;
				desc.onCommit = [editor, id, key](std::string_view text) {
					editor.edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, Ops::Quote(text)); });
				};
				UI::Widgets::TextField(context, row, desc);
				break;
			}
			case GraphSettingKind::Number: {
				f32 value[4] = {setting.defaultValue[0], 0.0f, 0.0f, 0.0f};
				ReadNumbers(json, value);
				UI::Widgets::NumberFieldDesc desc;
				desc.value = value[0];
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = FontRow();
				desc.onChange = [editor, id, key](f64 number) {
					const f32 one = static_cast<f32>(number);
					editor.edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, Numbers(&one, 1)); });
				};
				desc.onScrubBegin = [editor]() { editor.begin("Node Setting"); };
				desc.onScrubEnd = [editor]() { editor.end(); };
				UI::Widgets::NumberField(context, row, desc);
				break;
			}
			case GraphSettingKind::Color: {
				f32 value[4] = {setting.defaultValue[0], setting.defaultValue[1], setting.defaultValue[2], setting.defaultValue[3]};
				ReadNumbers(json, value);
				UI::Widgets::ColorSwatchField(
					context, row, Color(value[0], value[1], value[2], value[3]), UISystem::GetDefaultFont(), FontRow(),
					[editor, id, key](Color picked) {
						const f32 components[4] = {picked.r, picked.g, picked.b, picked.a};
						editor.edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, Numbers(components, 4)); });
					},
					[editor]() { editor.begin("Node Setting"); }, [editor]() { editor.end(); });
				break;
			}
			case GraphSettingKind::Vector: {
				f32 value[4] = {setting.defaultValue[0], setting.defaultValue[1], setting.defaultValue[2], setting.defaultValue[3]};
				ReadNumbers(json, value);
				UI::Widgets::VectorFieldDesc desc;
				desc.componentCount = 4;
				for (u32 i = 0; i < 4; ++i) {
					desc.values[i] = value[i];
				}
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = FontRow();
				desc.onChange = [editor, id, key](u32 component, f64 number) {
					GraphDocument* live = editor.document ? editor.document() : nullptr;
					GraphNode* held = live ? GraphDocumentOps::FindNode(*live, id) : nullptr;
					if (!held) {
						return;
					}
					f32 current[4] = {0.0f, 0.0f, 0.0f, 0.0f};
					if (const GraphSetting* now = FindSetting(held->settings, key)) {
						ReadNumbers(now->value, current);
					}
					current[component & 3] = static_cast<f32>(number);
					editor.edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, Numbers(current, 4)); });
				};
				UI::Widgets::VectorField(context, row, desc);
				break;
			}
			case GraphSettingKind::Gradient:
				BuildGradientEditor(context, parent, json, [editor, id, key](const str& gradient) {
					editor.edit("Gradient", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, gradient); });
				});
				break;
			case GraphSettingKind::Asset: {
				const str caption = AssetCaption(setting, json);
				Vector<str> extensions{setting.options.begin(), setting.options.end(), GetMemoryResource(MemTag::Editor)};
				auto takes = [extensions](const UI::DragPayload& payload) {
					Vector<std::string_view> views{extensions.begin(), extensions.end(), GetMemoryResource(MemTag::Editor)};
					GraphSettingDefinition kind;
					kind.options = views;
					return TakesAsset(kind, payload);
				};
				ObjectField::Desc field;
				field.caption = caption;
				field.accepts = takes;
				field.onDrop = [editor, id, key, takes](const UI::DragPayload& payload, Vec2) {
					if (!takes(payload)) {
						return;
					}
					const str value = "\"" + payload.data.As<AssetDragData>().guid.ToString() + "\"";
					editor.edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, value); });
				};
				field.onClear = [editor, id, key]() {
					editor.edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, "null"); });
				};
				field.resolveTarget = [editor, id, key]() -> EditorPingTarget {
					GraphDocument* live = editor.document ? editor.document() : nullptr;
					const GraphNode* held = live ? GraphDocumentOps::FindNode(*live, id) : nullptr;
					GraphSettingDefinition named;
					named.key = key;
					return {.asset = held ? HeldAsset(*held, named) : Guid{}};
				};
				ObjectField::Build(context, row, field);
				break;
			}
			case GraphSettingKind::Property:
			case GraphSettingKind::Block:
				break;
			}
		}
	}

	void BuildOpenGradients(UI::Context& context, UI::ElementHandle parent, const GraphNode& node, const GraphNodeDefinition& definition,
							const Editor& editor) {
		Vector<u32> active{GetMemoryResource(MemTag::Editor)};
		ActiveInputs(definition, node, active);
		const u32 id = node.id;
		for (u32 index : active) {
			const GraphPortDefinition& input = definition.inputs[index];
			if (input.type != GraphPortType::Gradient) {
				continue;
			}
			const GraphInput* held = FindInput(node, input.name);
			if (held && held->fromNode != 0) {
				continue;
			}
			AddText(context, parent, ColorToken::Text, MetricToken::FontRow, GraphPortLabel(input));
			const str json = held && !held->value.empty() ? held->value : str(input.defaultJson);
			BuildGradientEditor(context, parent, json, [editor, id, port = str(input.name)](const str& gradient) {
				editor.edit("Gradient", [&](GraphDocument& edited) { return NodeShaderEditOps::SetInputValue(edited, id, port, gradient); });
			});
		}
	}

	void OpenCurveEditor(UI::Context& context, UI::ElementHandle anchor, u32 node, const GraphPortDefinition& port, const Editor& editor) {
		const GraphDocument* document = editor.document ? editor.document() : nullptr;
		const GraphNode* held = document ? GraphDocumentOps::FindNode(const_cast<GraphDocument&>(*document), node) : nullptr;
		if (!held || !context.IsAlive(anchor)) {
			return;
		}
		const GraphInput* stored = FindInput(*held, port.name);
		std::string_view json = stored && !stored->value.empty() ? std::string_view(stored->value) : port.defaultJson;
		AnimationCurve curve(GetMemoryResource(MemTag::Editor));
		str error;
		if (!GraphCurves::Parse(json.empty() ? GraphCurves::DEFAULT_JSON : json, curve, error)) {
			return;
		}
		CurveEditor::Open(
			context, anchor, curve,
			[editor, node, name = str(port.name)](const AnimationCurve& edited) {
				const str text = GraphCurves::Write(edited);
				editor.edit("Edit Curve", [&](GraphDocument& changed) { return NodeShaderEditOps::SetInputValue(changed, node, name, text); });
			},
			[editor]() { editor.begin("Edit Curve"); }, [editor]() { editor.end(); });
	}

	void BuildGradientEditor(UI::Context& context, UI::ElementHandle parent, const str& json, std::function<void(const str&)> commit) {
		struct Gradient {
			str mode = "Blend";
			Vector<std::array<f32, 4>> colors{GetMemoryResource(MemTag::Editor)};
			Vector<std::array<f32, 2>> alphas{GetMemoryResource(MemTag::Editor)};
		};
		auto gradient = std::make_shared<Gradient>();
		if (!json.empty()) {
			LinearAllocator arena(json.size() + 256, MemTag::Editor);
			JSONValue value = JSONParse(json, arena);
			if (const JSONValue* mode = value.Find("mode"); mode && mode->IsString()) {
				gradient->mode = str(mode->AsString());
			}
			if (const JSONValue* colors = value.Find("colors"); colors && colors->IsArray()) {
				for (size_t i = 0; i < colors->Size(); ++i) {
					std::array<f32, 4> key{1.0f, 1.0f, 1.0f, 0.0f};
					for (size_t c = 0; c < 4 && c < (*colors)[i].Size(); ++c) {
						key[c] = static_cast<f32>((*colors)[i][c].AsNumber());
					}
					gradient->colors.push_back(key);
				}
			}
			if (const JSONValue* alphas = value.Find("alphas"); alphas && alphas->IsArray()) {
				for (size_t i = 0; i < alphas->Size(); ++i) {
					std::array<f32, 2> key{1.0f, 0.0f};
					for (size_t c = 0; c < 2 && c < (*alphas)[i].Size(); ++c) {
						key[c] = static_cast<f32>((*alphas)[i][c].AsNumber());
					}
					gradient->alphas.push_back(key);
				}
			}
		}
		if (gradient->colors.empty()) {
			gradient->colors.push_back({1.0f, 1.0f, 1.0f, 0.0f});
			gradient->colors.push_back({1.0f, 1.0f, 1.0f, 1.0f});
		}
		if (gradient->alphas.empty()) {
			gradient->alphas.push_back({1.0f, 0.0f});
			gradient->alphas.push_back({1.0f, 1.0f});
		}
		auto write = [gradient, commit]() {
			str out = "{\"alphas\":[";
			for (size_t i = 0; i < gradient->alphas.size(); ++i) {
				out += (i == 0 ? "" : ",") + Numbers(gradient->alphas[i].data(), 2);
			}
			out += "],\"colors\":[";
			for (size_t i = 0; i < gradient->colors.size(); ++i) {
				out += (i == 0 ? "" : ",") + Numbers(gradient->colors[i].data(), 4);
			}
			out += "],\"mode\":" + Ops::Quote(gradient->mode) + "}";
			commit(out);
		};
		static constexpr std::string_view MODES[] = {"Blend", "Fixed", "PerceptualBlend"};
		UI::ElementHandle modeRow = AddField(context, parent, "Mode");
		UI::Widgets::DropdownDesc mode;
		mode.items = MODES;
		for (u32 i = 0; i < std::size(MODES); ++i) {
			mode.selected = MODES[i] == gradient->mode ? i : mode.selected;
		}
		mode.font = UISystem::GetDefaultFont();
		mode.fontSizePx = FontRow();
		mode.onSelect = [gradient, write](u32 index) {
			gradient->mode = str(MODES[index % std::size(MODES)]);
			write();
		};
		UI::Widgets::Dropdown(context, modeRow, mode);

		auto number = [&](UI::ElementHandle row, f32 value, std::function<void(f32)> set) {
			UI::Widgets::NumberFieldDesc desc;
			desc.value = value;
			desc.minValue = 0.0;
			desc.maxValue = 1.0;
			desc.decimals = 3;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onChange = [set](f64 changed) { set(static_cast<f32>(changed)); };
			UI::Widgets::NumberField(context, row, desc);
		};
		auto button = [&](UI::ElementHandle row, std::string_view label, std::function<void()> onClick) {
			UI::Widgets::ButtonDesc desc;
			desc.label = label;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			desc.onClick = std::move(onClick);
			UI::Widgets::Button(context, row, desc);
		};
		constexpr size_t MAX_KEYS = 8;
		for (size_t i = 0; i < gradient->colors.size(); ++i) {
			UI::ElementHandle row = AddField(context, parent, std::format("Color {}", i + 1));
			const std::array<f32, 4>& key = gradient->colors[i];
			UI::Widgets::ColorSwatchField(context, row, Color(key[0], key[1], key[2], 1.0f), UISystem::GetDefaultFont(), FontRow(),
										  [gradient, write, i](Color picked) {
											  gradient->colors[i][0] = picked.r;
											  gradient->colors[i][1] = picked.g;
											  gradient->colors[i][2] = picked.b;
											  write();
										  });
			number(row, key[3], [gradient, write, i](f32 time) {
				gradient->colors[i][3] = time;
				write();
			});
			if (gradient->colors.size() > 2) {
				button(row, "-", [gradient, write, i]() {
					gradient->colors.erase(gradient->colors.begin() + static_cast<std::ptrdiff_t>(i));
					write();
				});
			}
		}
		for (size_t i = 0; i < gradient->alphas.size(); ++i) {
			UI::ElementHandle row = AddField(context, parent, std::format("Alpha {}", i + 1));
			number(row, gradient->alphas[i][0], [gradient, write, i](f32 alpha) {
				gradient->alphas[i][0] = alpha;
				write();
			});
			number(row, gradient->alphas[i][1], [gradient, write, i](f32 time) {
				gradient->alphas[i][1] = time;
				write();
			});
			if (gradient->alphas.size() > 2) {
				button(row, "-", [gradient, write, i]() {
					gradient->alphas.erase(gradient->alphas.begin() + static_cast<std::ptrdiff_t>(i));
					write();
				});
			}
		}
		UI::ElementHandle adds = AddRow(context, parent);
		if (gradient->colors.size() < MAX_KEYS) {
			button(adds, "Add Color Key", [gradient, write]() {
				gradient->colors.push_back({1.0f, 1.0f, 1.0f, 0.5f});
				write();
			});
		}
		if (gradient->alphas.size() < MAX_KEYS) {
			button(adds, "Add Alpha Key", [gradient, write]() {
				gradient->alphas.push_back({1.0f, 0.5f});
				write();
			});
		}
	}

	void BuildPropertyDefault(UI::Context& context, UI::ElementHandle row, u32 id, const Editor& editor) {
		const GraphDocument* document = editor.document ? editor.document() : nullptr;
		const GraphProperty* property = document ? FindProperty(*document, id) : nullptr;
		if (!property || !context.IsAlive(row)) {
			return;
		}
		GraphPropertyInfo info;
		str refused;
		GraphProperties::Parse(*property, info, refused);
		auto setDefault = [editor, id](str json) {
			editor.edit("Property Default", [&](GraphDocument& edited) { return Ops::SetPropertyDefault(edited, id, json); });
		};
		f32 value[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		const u32 count = ReadNumbers(property->defaultValue, value);
		if (property->type == "Float") {
			UI::Widgets::NumberFieldDesc desc;
			desc.value = value[0];
			desc.decimals = info.kind == GraphPropertyKind::Integer ? 0 : UI::Widgets::DEFAULT_DECIMALS;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onChange = [setDefault](f64 number) {
				const f32 one = static_cast<f32>(number);
				setDefault(Numbers(&one, 1));
			};
			desc.onScrubBegin = [editor]() { editor.begin("Property Default"); };
			desc.onScrubEnd = [editor]() { editor.end(); };
			UI::Widgets::NumberField(context, row, desc);
		} else if (property->type == "Boolean" || property->type == "BooleanKeyword") {
			UI::Widgets::Checkbox(context, row, value[0] != 0.0f, [setDefault](bool on) { setDefault(on ? "true" : "false"); });
		} else if (property->type == "Color") {
			if (count == 0) {
				value[3] = 1.0f;
			}
			UI::Widgets::ColorSwatchField(
				context, row, Color(value[0], value[1], value[2], value[3]), UISystem::GetDefaultFont(), FontRow(),
				[setDefault](Color picked) {
					const f32 components[4] = {picked.r, picked.g, picked.b, picked.a};
					setDefault(Numbers(components, 4));
				},
				[editor]() { editor.begin("Property Default"); }, [editor]() { editor.end(); });
		} else if (property->type.starts_with("Vector")) {
			const u32 width = property->type == "Vector2" ? 2u : (property->type == "Vector3" ? 3u : 4u);
			UI::Widgets::VectorFieldDesc desc;
			desc.componentCount = width;
			for (u32 i = 0; i < 4; ++i) {
				desc.values[i] = value[i];
			}
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onChange = [editor, id, width, setDefault](u32 component, f64 number) {
				const GraphDocument* live = editor.document ? editor.document() : nullptr;
				const GraphProperty* now = live ? FindProperty(*live, id) : nullptr;
				f32 current[4] = {0.0f, 0.0f, 0.0f, 0.0f};
				if (now) {
					ReadNumbers(now->defaultValue, current);
				}
				current[component & 3] = static_cast<f32>(number);
				setDefault(Numbers(current, width));
			};
			UI::Widgets::VectorField(context, row, desc);
		}
	}

	void DescribeGroups(const GraphDocument& document, Vector<GraphGroupDesc>& out) {
		out.clear();
		for (const GraphGroup& group : document.groups) {
			GraphGroupDesc desc;
			desc.id = group.id;
			desc.title = group.title;
			desc.position = Vec2(static_cast<f32>(group.x), static_cast<f32>(group.y));
			for (const GraphNode& node : document.nodes) {
				if (node.group == group.id) {
					desc.nodes.push_back(node.id);
				}
			}
			for (const GraphContext& stack : document.contexts) {
				if (stack.node.group == group.id) {
					desc.nodes.push_back(stack.node.id);
				}
			}
			for (const GraphNote& note : document.notes) {
				if (note.group == group.id) {
					desc.notes.push_back(note.id);
				}
			}
			out.push_back(std::move(desc));
		}
	}

	void DescribeNotes(const GraphDocument& document, Vector<GraphNoteDesc>& out) {
		out.clear();
		for (const GraphNote& note : document.notes) {
			GraphNoteDesc desc;
			desc.id = note.id;
			desc.position = Vec2(static_cast<f32>(note.x), static_cast<f32>(note.y));
			desc.size = Vec2(static_cast<f32>(note.width), static_cast<f32>(note.height));
			desc.title = note.title;
			desc.body = note.body;
			desc.theme = note.theme;
			desc.textSize = note.size;
			out.push_back(std::move(desc));
		}
	}

	void ResizeNote(GraphDocument& document, u64 note, Vec2 size) {
		for (const GraphNote& held : document.notes) {
			if (held.id == note) {
				GraphNote resized = held;
				resized.width = static_cast<i32>(std::lround(size.x));
				resized.height = static_cast<i32>(std::lround(size.y));
				GraphDocumentOps::SetNote(document, resized);
				return;
			}
		}
	}

	void SetGroupOf(GraphDocument& document, std::span<const GraphItem> items, u64 group) {
		Vector<u32> members{GetMemoryResource(MemTag::Editor)};
		for (const GraphItem& item : items) {
			if (item.kind == GraphItemKind::Node || item.kind == GraphItemKind::Note) {
				members.push_back(static_cast<u32>(item.id));
			}
		}
		GraphDocumentOps::SetGroup(document, members, static_cast<u32>(group));
	}

	bool EditNote(GraphDocument& document, u64 note, std::string_view title, std::string_view body) {
		for (const GraphNote& held : document.notes) {
			if (held.id == note) {
				GraphNote edited = held;
				edited.title = str(title);
				edited.body = str(body);
				return GraphDocumentOps::SetNote(document, edited);
			}
		}
		return false;
	}

	void GroupMenu(Vector<UI::Widgets::MenuItem>& items, const Editor& editor, u64 group, std::function<u32(GraphDocument&, std::span<const u32>)> remove) {
		items.push_back({"Delete Group", "", true, false, [editor, group]() {
							 editor.edit("Delete Group", [&](GraphDocument& document) {
								 const u32 ids[] = {static_cast<u32>(group)};
								 return GraphDocumentOps::Delete(document, ids) > 0;
							 });
						 }});
		items.push_back({"Delete Group and Contents", "", true, false, [editor, group, remove]() {
							 editor.edit("Delete Group and Contents", [&](GraphDocument& document) {
								 Vector<u32> ids{GetMemoryResource(MemTag::Editor)};
								 ids.push_back(static_cast<u32>(group));
								 for (const GraphNode& node : document.nodes) {
									 if (node.group == group) {
										 ids.push_back(node.id);
									 }
								 }
								 for (const GraphContext& context : document.contexts) {
									 if (context.node.group == group) {
										 ids.push_back(context.node.id);
									 }
								 }
								 for (const GraphNote& note : document.notes) {
									 if (note.group == group) {
										 ids.push_back(note.id);
									 }
								 }
								 return remove(document, ids) > 0;
							 });
						 }});
	}

	void NoteMenu(Vector<UI::Widgets::MenuItem>& items, const Editor& editor, u64 note) {
		auto restyle = [editor, note](std::string_view label, auto change) {
			return [editor, note, label = str(label), change]() {
				editor.edit(label, [&](GraphDocument& document) {
					for (const GraphNote& held : document.notes) {
						if (held.id == note) {
							GraphNote edited = held;
							change(edited);
							return GraphDocumentOps::SetNote(document, edited);
						}
					}
					return false;
				});
			};
		};
		UI::Widgets::MenuItem theme{"Theme"};
		theme.children.push_back({"Light", "", true, false, restyle("Sticky Note Theme", [](GraphNote& n) { n.theme = GraphNoteTheme::Light; })});
		theme.children.push_back({"Dark", "", true, false, restyle("Sticky Note Theme", [](GraphNote& n) { n.theme = GraphNoteTheme::Dark; })});
		items.push_back(std::move(theme));
		UI::Widgets::MenuItem size{"Text Size"};
		size.children.push_back({"Small", "", true, false, restyle("Sticky Note Text Size", [](GraphNote& n) { n.size = GraphNoteSize::Small; })});
		size.children.push_back({"Medium", "", true, false, restyle("Sticky Note Text Size", [](GraphNote& n) { n.size = GraphNoteSize::Medium; })});
		size.children.push_back({"Large", "", true, false, restyle("Sticky Note Text Size", [](GraphNote& n) { n.size = GraphNoteSize::Large; })});
		size.children.push_back({"Huge", "", true, false, restyle("Sticky Note Text Size", [](GraphNote& n) { n.size = GraphNoteSize::Huge; })});
		items.push_back(std::move(size));
		items.push_back({"Delete", "", true, true, [editor, note]() {
							 editor.edit("Delete Sticky Note", [&](GraphDocument& document) {
								 const u32 ids[] = {static_cast<u32>(note)};
								 return GraphDocumentOps::Delete(document, ids) > 0;
							 });
						 }});
	}

	i32 JoiningPort(const GraphDocument& document, GraphDefinitions& definitions, const GraphNodeDefinition& definition, bool fromOutput,
					bool fromInput, u32 fromNode, std::string_view fromPort) {
		if (fromOutput) {
			const GraphNode fresh;
			for (u32 i = 0; i < definition.inputs.size(); ++i) {
				if (GraphNodeLibrary::InputActive(definition, i, fresh) &&
					Ops::OutputFeeds(document, fromNode, fromPort, definition.inputs[i].type, &definitions)) {
					return static_cast<i32>(i);
				}
			}
		} else if (fromInput) {
			for (u32 i = 0; i < definition.outputs.size(); ++i) {
				if (Ops::InputTakes(document, fromNode, fromPort, definition.outputs[i].type, &definitions)) {
					return static_cast<i32>(i);
				}
			}
		}
		return -1;
	}

	void LibraryEntries(const GraphDocument& document, GraphDefinitions& definitions, const std::function<bool(const GraphNodeDefinition&)>& offered,
						bool fromOutput, bool fromInput, u32 fromNode, std::string_view fromPort, Vector<GraphSearchEntry>& out) {
		const std::span<const GraphNodeDefinition* const> library = GraphNodeLibrary::All();
		for (u32 index = 0; index < library.size(); ++index) {
			const GraphNodeDefinition& definition = *library[index];
			if (!offered(definition)) {
				continue;
			}
			const i32 port = JoiningPort(document, definitions, definition, fromOutput, fromInput, fromNode, fromPort);
			if (port < 0 && (fromOutput || fromInput)) {
				continue;
			}
			GraphSearchEntry entry;
			entry.name = str(definition.name);
			entry.category = str(definition.category);
			entry.synonyms = str(definition.synonyms);
			if (port >= 0) {
				entry.detail = str(fromOutput ? GraphPortLabel(definition.inputs[static_cast<size_t>(port)])
											  : GraphPortLabel(definition.outputs[static_cast<size_t>(port)]));
			}
			entry.key = (static_cast<u64>(index) << LIBRARY_PORT_BITS) | static_cast<u64>(port < 0 ? 0 : port);
			out.push_back(std::move(entry));
		}
	}

	u32 MakeLibraryNode(GraphDocument& document, GraphDefinitions& definitions, u64 key, Vec2 graph, bool fromOutput, bool fromInput, u32 fromNode,
						std::string_view fromPort) {
		const std::span<const GraphNodeDefinition* const> library = GraphNodeLibrary::All();
		const u64 index = key >> LIBRARY_PORT_BITS;
		const u32 port = static_cast<u32>(key & ((1ull << LIBRARY_PORT_BITS) - 1));
		if (index >= library.size()) {
			return 0;
		}
		const u32 created = Ops::AddNode(document, library[index]->id, graph);
		if (created != 0) {
			JoinDroppedWire(document, definitions, created, port, fromOutput, fromInput, fromNode, fromPort);
		}
		return created;
	}

	void JoinDroppedWire(GraphDocument& document, GraphDefinitions& definitions, u32 created, u32 port, bool fromOutput, bool fromInput, u32 fromNode,
						 std::string_view fromPort) {
		if (!fromOutput && !fromInput) {
			return;
		}
		const GraphNode* made = GraphDocumentOps::FindNode(document, created);
		const GraphNodeDefinition* definition = made ? definitions.Find(document, *made) : nullptr;
		if (definition && fromOutput && port < definition->inputs.size()) {
			const str input(definition->inputs[port].name);
			if (Ops::CanConnect(document, fromNode, fromPort, created, input, nullptr, &definitions)) {
				GraphDocumentOps::Connect(document, fromNode, fromPort, created, input);
			}
		} else if (definition && fromInput && port < definition->outputs.size()) {
			const str output(definition->outputs[port].name);
			if (Ops::CanConnect(document, created, output, fromNode, fromPort, nullptr, &definitions)) {
				GraphDocumentOps::Connect(document, created, output, fromNode, fromPort);
			}
		}
	}

	Vec2 FragmentCorner(std::string_view text, Vec2 fallback) {
		GraphDocument fragment;
		str error;
		if (!GraphDocumentOps::Read(text, fragment, error)) {
			return fallback;
		}
		Vec2 corner{1.0e9f, 1.0e9f};
		auto take = [&](i32 x, i32 y) {
			corner.x = std::min(corner.x, static_cast<f32>(x));
			corner.y = std::min(corner.y, static_cast<f32>(y));
		};
		for (const GraphNode& node : fragment.nodes) {
			take(node.x, node.y);
		}
		for (const GraphContext& context : fragment.contexts) {
			take(context.node.x, context.node.y);
		}
		for (const GraphNote& note : fragment.notes) {
			take(note.x, note.y);
		}
		return corner.x > 1.0e8f ? fallback : corner;
	}

	void PastedItems(const GraphDocument& document, std::span<const u32> pasted, Vector<GraphItem>& out) {
		out.clear();
		for (u32 id : pasted) {
			const bool note = std::any_of(document.notes.begin(), document.notes.end(), [&](const GraphNote& n) { return n.id == id; });
			const bool group = std::any_of(document.groups.begin(), document.groups.end(), [&](const GraphGroup& g) { return g.id == id; });
			const bool block = std::any_of(document.contexts.begin(), document.contexts.end(), [&](const GraphContext& c) {
				return std::any_of(c.blocks.begin(), c.blocks.end(), [&](const GraphNode& b) { return b.id == id; });
			});
			out.push_back({note ? GraphItemKind::Note : (group ? GraphItemKind::Group : (block ? GraphItemKind::Block : GraphItemKind::Node)), id});
		}
	}

} // namespace Shalltear::GraphWindowParts
