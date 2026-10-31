#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphSetting {
		str key;
		str value;
	};

	struct GraphInput {
		str port;
		u32 fromNode = 0;
		str fromPort;
		str value;
		str space;
	};

	struct GraphNode {
		u32 id = 0;
		str def;
		i32 x = 0;
		i32 y = 0;
		u32 group = 0;
		bool previewCollapsed = false;
		bool disabled = false;
		Vector<GraphSetting> settings{GetMemoryResource(MemTag::Editor)};
		Vector<GraphInput> inputs{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphFlow {
		u32 from = 0;
		str slot;
	};

	struct GraphContext {
		GraphNode node;
		Vector<GraphNode> blocks{GetMemoryResource(MemTag::Editor)};
		Vector<GraphFlow> flows{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphProperty {
		u32 id = 0;
		str name;
		str reference;
		str type;
		str defaultValue;
		Vector<GraphSetting> settings{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphGroup {
		u32 id = 0;
		str title;
		i32 x = 0;
		i32 y = 0;
	};

	enum class GraphNoteTheme : u8 {
		Light,
		Dark
	};
	enum class GraphNoteSize : u8 {
		Small,
		Medium,
		Large,
		Huge
	};

	struct GraphNote {
		u32 id = 0;
		str title;
		str body;
		i32 x = 0;
		i32 y = 0;
		i32 width = 200;
		i32 height = 160;
		GraphNoteTheme theme = GraphNoteTheme::Light;
		GraphNoteSize size = GraphNoteSize::Medium;
		u32 group = 0;
	};

	struct GraphDocument {
		str type;
		u32 nextId = 1;
		Vector<GraphSetting> settings{GetMemoryResource(MemTag::Editor)};
		Vector<GraphProperty> properties{GetMemoryResource(MemTag::Editor)};
		Vector<GraphNode> nodes{GetMemoryResource(MemTag::Editor)};
		Vector<GraphContext> contexts{GetMemoryResource(MemTag::Editor)};
		Vector<GraphGroup> groups{GetMemoryResource(MemTag::Editor)};
		Vector<GraphNote> notes{GetMemoryResource(MemTag::Editor)};
	};

	namespace GraphDocumentOps {

		inline constexpr u32 FORMAT_VERSION = 4;

		inline constexpr std::string_view REDIRECT_DEF = "Utility/Redirect";
		inline constexpr std::string_view REDIRECT_IN = "In";
		inline constexpr std::string_view REDIRECT_OUT = "Out";

		SHALLTEAR_EDITOR_API bool Read(std::string_view text, GraphDocument& out, str& error);
		SHALLTEAR_EDITOR_API str Write(const GraphDocument& document);

		SHALLTEAR_EDITOR_API GraphNode* FindNode(GraphDocument& document, u32 id);

		SHALLTEAR_EDITOR_API bool MoveNode(GraphDocument& document, u32 id, Vec2 position);

		SHALLTEAR_EDITOR_API bool Connect(GraphDocument& document, u32 fromNode, std::string_view fromPort, u32 toNode,
										  std::string_view toPort);
		SHALLTEAR_EDITOR_API bool Disconnect(GraphDocument& document, u32 toNode, std::string_view toPort);

		SHALLTEAR_EDITOR_API bool Link(GraphDocument& document, u32 from, u32 to, std::string_view slot);
		SHALLTEAR_EDITOR_API bool Unlink(GraphDocument& document, u32 from, u32 to, std::string_view slot);
		SHALLTEAR_EDITOR_API bool MoveBlocks(GraphDocument& document, std::span<const u32> blocks, u32 context, u32 index);
		SHALLTEAR_EDITOR_API bool SetBlockEnabled(GraphDocument& document, u32 block, bool enabled);
		SHALLTEAR_EDITOR_API bool SetInputSpace(GraphDocument& document, u32 node, std::string_view port, std::string_view space);
		SHALLTEAR_EDITOR_API GraphContext* ContextOf(GraphDocument& document, u32 block);

		SHALLTEAR_EDITOR_API u32 Delete(GraphDocument& document, std::span<const u32> ids);

		SHALLTEAR_EDITOR_API u32 AddGroup(GraphDocument& document, std::string_view title, std::span<const u32> members,
										  Vec2 position);
		SHALLTEAR_EDITOR_API bool SetGroup(GraphDocument& document, std::span<const u32> members, u32 group);
		SHALLTEAR_EDITOR_API bool SetGroupTitle(GraphDocument& document, u32 group, std::string_view title);

		SHALLTEAR_EDITOR_API u32 AddNote(GraphDocument& document, const GraphNote& note);
		SHALLTEAR_EDITOR_API bool SetNote(GraphDocument& document, const GraphNote& note);

		SHALLTEAR_EDITOR_API u32 InsertRedirect(GraphDocument& document, u32 toNode, std::string_view toPort, Vec2 position);

		SHALLTEAR_EDITOR_API str Copy(const GraphDocument& document, std::span<const u32> ids);

		SHALLTEAR_EDITOR_API bool Paste(GraphDocument& document, std::string_view fragment, Vec2 offset, Vector<u32>& pasted,
										str& error);

		SHALLTEAR_EDITOR_API bool Duplicate(GraphDocument& document, std::span<const u32> ids, Vec2 offset,
											Vector<u32>& pasted);

	} // namespace GraphDocumentOps

} // namespace Shalltear
