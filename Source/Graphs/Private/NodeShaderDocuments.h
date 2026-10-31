#pragma once

#include "GraphsAPI.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	struct GraphDocument;

	namespace NodeShaderDocuments {

		using Upgrade = void (*)(GraphDocument& document);

		SHALLTEAR_GRAPHS_API bool Open(const Guid& asset, const void* owner, str& error, Upgrade upgrade = nullptr);
		SHALLTEAR_GRAPHS_API void Close(const Guid& asset);
		SHALLTEAR_GRAPHS_API bool IsOpen(const Guid& asset);

		SHALLTEAR_GRAPHS_API GraphDocument* Get(const Guid& asset);

		SHALLTEAR_GRAPHS_API void BeginEdit(const Guid& asset, std::string_view label);
		SHALLTEAR_GRAPHS_API void EndEdit(const Guid& asset);

		SHALLTEAR_GRAPHS_API u64 GetRevision(const Guid& asset);

		SHALLTEAR_GRAPHS_API bool HasUnsavedChanges(const Guid& asset);
		SHALLTEAR_GRAPHS_API bool Save(const Guid& asset);
		SHALLTEAR_GRAPHS_API Guid SaveAs(const Guid& asset, const Path& path);
		SHALLTEAR_GRAPHS_API bool Discard(const Guid& asset);

		SHALLTEAR_GRAPHS_API Path GetPath(const Guid& asset);

		SHALLTEAR_GRAPHS_API u64 GetSaveSerial();

	} // namespace NodeShaderDocuments

} // namespace Shalltear
