#pragma once

#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>
#include <string_view>

namespace Shalltear {

	struct EditorDocumentDesc {
		Guid asset;
		const void* owner = nullptr;
		std::function<void(str& out)> capture;
		std::function<bool(std::string_view bytes)> restore;
	};

	namespace EditorDocuments {

		SHALLTEAR_EDITOR_API bool Open(EditorDocumentDesc desc);
		SHALLTEAR_EDITOR_API void Close(const Guid& asset);
		SHALLTEAR_EDITOR_API bool IsOpen(const Guid& asset);

		SHALLTEAR_EDITOR_API void BeginEdit(const Guid& asset, std::string_view label);
		SHALLTEAR_EDITOR_API void EndEdit(const Guid& asset);

		SHALLTEAR_EDITOR_API u64 GetRevision(const Guid& asset);

		SHALLTEAR_EDITOR_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_EDITOR_API void Terminate();

	} // namespace EditorDocuments

} // namespace Shalltear
