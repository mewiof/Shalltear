#pragma once

#include "GraphsAPI.h"

#include "GraphCompiler.h"
#include "GraphNodeLibrary.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GraphCanvas.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {

	class GraphDefinitions;

	namespace GraphWindowParts {

		SHALLTEAR_GRAPHS_API str Numbers(const f32* values, u32 count);
		SHALLTEAR_GRAPHS_API u32 ReadNumbers(std::string_view json, f32 out[4]);
		SHALLTEAR_GRAPHS_API str ReadString(std::string_view json);
		SHALLTEAR_GRAPHS_API const GraphInput* FindInput(const GraphNode& node, std::string_view port);
		SHALLTEAR_GRAPHS_API const GraphSetting* FindSetting(const Vector<GraphSetting>& settings, std::string_view key);
		SHALLTEAR_GRAPHS_API const GraphProperty* FindProperty(const GraphDocument& document, u32 id);

		SHALLTEAR_GRAPHS_API void ActiveInputs(const GraphNodeDefinition& definition, const GraphNode& node, Vector<u32>& out);
		SHALLTEAR_GRAPHS_API GraphInlineValue InlineFor(const GraphNode& node, const GraphPortDefinition& port, u32 dynamicWidth, bool color);
		SHALLTEAR_GRAPHS_API str SettingText(const GraphNode& node, const GraphSettingDefinition& setting);

		SHALLTEAR_GRAPHS_API Guid HeldAsset(const GraphNode& node, const GraphSettingDefinition& setting);
		SHALLTEAR_GRAPHS_API str AssetCaption(const GraphSettingDefinition& setting, std::string_view json);
		SHALLTEAR_GRAPHS_API bool TakesAsset(const GraphSettingDefinition& setting, const UI::DragPayload& payload);

		SHALLTEAR_GRAPHS_API UI::ElementHandle AddText(UI::Context& context, UI::ElementHandle parent, EditorTheme::ColorToken color,
													   EditorTheme::MetricToken size, std::string_view text);
		SHALLTEAR_GRAPHS_API UI::ElementHandle AddRow(UI::Context& context, UI::ElementHandle parent);
		SHALLTEAR_GRAPHS_API UI::ElementHandle AddField(UI::Context& context, UI::ElementHandle parent, std::string_view label);
		SHALLTEAR_GRAPHS_API f32 FontRow();

		struct Editor {
			std::function<bool(std::string_view label, const std::function<bool(GraphDocument&)>& op)> edit;
			std::function<void(std::string_view label)> begin;
			std::function<void()> end;
			std::function<GraphDocument*()> document;
		};

		SHALLTEAR_GRAPHS_API void BuildSettings(UI::Context& context, UI::ElementHandle parent, u32 node,
												std::span<const GraphSettingDefinition> settings, const Editor& editor);
		SHALLTEAR_GRAPHS_API void BuildGradientEditor(UI::Context& context, UI::ElementHandle parent, const str& json,
													  std::function<void(const str&)> commit);
		SHALLTEAR_GRAPHS_API void BuildOpenGradients(UI::Context& context, UI::ElementHandle parent, const GraphNode& node,
													 const GraphNodeDefinition& definition, const Editor& editor);
		SHALLTEAR_GRAPHS_API void OpenCurveEditor(UI::Context& context, UI::ElementHandle anchor, u32 node, const GraphPortDefinition& port,
												  const Editor& editor);
		SHALLTEAR_GRAPHS_API void BuildPropertyDefault(UI::Context& context, UI::ElementHandle row, u32 property, const Editor& editor);

		SHALLTEAR_GRAPHS_API void DescribeGroups(const GraphDocument& document, Vector<GraphGroupDesc>& out);
		SHALLTEAR_GRAPHS_API void DescribeNotes(const GraphDocument& document, Vector<GraphNoteDesc>& out);
		SHALLTEAR_GRAPHS_API void ResizeNote(GraphDocument& document, u64 note, Vec2 size);
		SHALLTEAR_GRAPHS_API void SetGroupOf(GraphDocument& document, std::span<const GraphItem> items, u64 group);
		SHALLTEAR_GRAPHS_API bool EditNote(GraphDocument& document, u64 note, std::string_view title, std::string_view body);
		SHALLTEAR_GRAPHS_API void GroupMenu(Vector<UI::Widgets::MenuItem>& items, const Editor& editor, u64 group,
											std::function<u32(GraphDocument&, std::span<const u32>)> remove);
		SHALLTEAR_GRAPHS_API void NoteMenu(Vector<UI::Widgets::MenuItem>& items, const Editor& editor, u64 note);

		inline constexpr u64 LIBRARY_PORT_BITS = 8;

		SHALLTEAR_GRAPHS_API i32 JoiningPort(const GraphDocument& document, GraphDefinitions& definitions, const GraphNodeDefinition& definition,
											 bool fromOutput, bool fromInput, u32 fromNode, std::string_view fromPort);
		SHALLTEAR_GRAPHS_API void LibraryEntries(const GraphDocument& document, GraphDefinitions& definitions,
												 const std::function<bool(const GraphNodeDefinition&)>& offered, bool fromOutput, bool fromInput,
												 u32 fromNode, std::string_view fromPort, Vector<GraphSearchEntry>& out);
		SHALLTEAR_GRAPHS_API u32 MakeLibraryNode(GraphDocument& document, GraphDefinitions& definitions, u64 key, Vec2 graph, bool fromOutput,
												 bool fromInput, u32 fromNode, std::string_view fromPort);
		SHALLTEAR_GRAPHS_API void JoinDroppedWire(GraphDocument& document, GraphDefinitions& definitions, u32 created, u32 port, bool fromOutput,
												  bool fromInput, u32 fromNode, std::string_view fromPort);

		SHALLTEAR_GRAPHS_API Vec2 FragmentCorner(std::string_view fragment, Vec2 fallback);
		SHALLTEAR_GRAPHS_API void PastedItems(const GraphDocument& document, std::span<const u32> pasted, Vector<GraphItem>& out);

		inline EditorTheme::ColorToken ColorOf(GraphDiagnosticSeverity severity) {
			return severity == GraphDiagnosticSeverity::Error ? EditorTheme::ColorToken::StatusError : EditorTheme::ColorToken::StatusWarn;
		}
		template <typename Fn>
		EditorTheme::ColorToken MarkOf(std::span<const GraphDiagnostic> diagnostics, Fn&& said) {
			bool warned = false;
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				if (!said(diagnostic)) {
					continue;
				}
				if (diagnostic.severity == GraphDiagnosticSeverity::Error) {
					return EditorTheme::ColorToken::StatusError;
				}
				warned = true;
			}
			return warned ? EditorTheme::ColorToken::StatusWarn : EditorTheme::ColorToken::CardEdge;
		}
		template <typename Fn>
		str TooltipOf(std::span<const GraphDiagnostic> diagnostics, Fn&& said) {
			str out;
			for (const GraphDiagnosticSeverity severity : {GraphDiagnosticSeverity::Error, GraphDiagnosticSeverity::Warning}) {
				for (const GraphDiagnostic& diagnostic : diagnostics) {
					if (diagnostic.severity == severity && said(diagnostic)) {
						out += (out.empty() ? "" : "\n") + diagnostic.message;
					}
				}
			}
			return out;
		}

	} // namespace GraphWindowParts

} // namespace Shalltear
