#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UIDocument.h"

#include <span>
#include <string_view>

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	namespace UIDocumentOps {

		struct Snapshot {
			UIDocumentNode root;
			Vector<UIDocumentSheet> sheets{GetMemoryResource(MemTag::Editor)};
			Vector<UIDocumentTemplate> templates{GetMemoryResource(MemTag::Editor)};
		};

		SHALLTEAR_EDITOR_API void Capture(const UIDocumentAsset& document, Snapshot& out);
		SHALLTEAR_EDITOR_API void Restore(UIDocumentAsset& document, const Snapshot& snapshot);
		SHALLTEAR_EDITOR_API UIDocumentNode CloneNode(const UIDocumentNode& node);

		SHALLTEAR_EDITOR_API bool Validate(const UIDocumentAsset& document, str& error);

		struct Location {
			u32 parent = 0xFFFFFFFFu;
			u32 childIndex = 0;
		};
		SHALLTEAR_EDITOR_API Location LocationOf(const UIDocumentAsset& document, u32 node);

		SHALLTEAR_EDITOR_API u32 ChildAt(const UIDocumentAsset& document, u32 parent, u32 childIndex);

		SHALLTEAR_EDITOR_API u32 GetChildCount(const UIDocumentAsset& document, u32 node);

		SHALLTEAR_EDITOR_API bool IsInSubtree(const UIDocumentAsset& document, u32 ancestor, u32 node);

		SHALLTEAR_EDITOR_API void GetOutermost(const UIDocumentAsset& document, std::span<const u32> nodes,
											   Vector<u32>& out);

		SHALLTEAR_EDITOR_API bool CanTakeChildren(const UIDocumentAsset& document, u32 node);

		SHALLTEAR_EDITOR_API bool Insert(UIDocumentAsset& document, u32 parent, u32 childIndex, const UIDocumentNode& node,
										 str& error);
		SHALLTEAR_EDITOR_API bool Remove(UIDocumentAsset& document, u32 node, str& error);
		SHALLTEAR_EDITOR_API bool Duplicate(UIDocumentAsset& document, u32 node, str& error);
		SHALLTEAR_EDITOR_API bool Move(UIDocumentAsset& document, u32 node, u32 newParent, u32 childIndex, str& error);
		SHALLTEAR_EDITOR_API bool MoveNodes(UIDocumentAsset& document, std::span<const u32> nodes, u32 newParent,
											u32 childIndex, str& error);

		SHALLTEAR_EDITOR_API UIDocumentNode MakeElement(std::string_view tag);

		SHALLTEAR_EDITOR_API str UniqueId(const UIDocumentAsset& document, std::string_view base);

		SHALLTEAR_EDITOR_API bool SetId(UIDocumentAsset& document, u32 node, std::string_view id, str& error);
		SHALLTEAR_EDITOR_API bool SetText(UIDocumentAsset& document, u32 node, std::string_view text, str& error);
		SHALLTEAR_EDITOR_API bool SetStyle(UIDocumentAsset& document, u32 node, std::string_view style, str& error);
		SHALLTEAR_EDITOR_API bool SetClasses(UIDocumentAsset& document, u32 node, std::span<const std::string_view> classes,
											 str& error);
		SHALLTEAR_EDITOR_API bool SetAttribute(UIDocumentAsset& document, u32 node, std::string_view name,
											   std::string_view value, str& error);

		SHALLTEAR_EDITOR_API bool CreateTemplate(UIDocumentAsset& document, u32 node, std::string_view virtualPath,
												 str& error);

		SHALLTEAR_EDITOR_API bool Unpack(UIDocumentAsset& document, u32 instance, bool completely, str& error);

		SHALLTEAR_EDITOR_API bool Save(UIDocumentAsset& document);

	} // namespace UIDocumentOps

} // namespace Shalltear
